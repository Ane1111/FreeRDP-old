/*
   FreeRDP: A Remote Desktop Protocol client.
   Video Redirection Virtual Channel

   Copyright 2010-2011 Vic Lee

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsmf_constants.h"
#include "tsmf_types.h"
#include "tsmf_ifman.h"

static int
tsmf_on_data_received(IWTSVirtualChannelCallback * pChannelCallback,
	uint32 cbSize,
	char * pBuffer)
{
	TSMF_CHANNEL_CALLBACK * callback = (TSMF_CHANNEL_CALLBACK *) pChannelCallback;
	TSMF_IFMAN ifman = { 0 };
	uint32 InterfaceId;
	uint32 MessageId;
	uint32 FunctionId;
	int error = -1;
	uint32 out_size;
	char * out_data;

	/* 2.2.1 Shared Message Header (SHARED_MSG_HEADER) */
	if (cbSize < 12)
	{
		LLOGLN(0, ("tsmf_on_data_received: invalid size. cbSize=%d", cbSize));
		return 1;
	}
	InterfaceId = GET_UINT32(pBuffer, 0);
	MessageId = GET_UINT32(pBuffer, 4);
	FunctionId = GET_UINT32(pBuffer, 8);
	LLOGLN(10, ("tsmf_on_data_received: cbSize=%d InterfaceId=0x%X MessageId=0x%X FunctionId=0x%X",
		cbSize, InterfaceId, MessageId, FunctionId));

	ifman.input_buffer = pBuffer + 12;
	ifman.input_buffer_size = cbSize - 12;
	ifman.output_buffer = NULL;
	ifman.output_buffer_size = 0;
	ifman.output_pending = 0;
	ifman.output_interface_id = InterfaceId;

	switch (InterfaceId)
	{
		case TSMF_INTERFACE_CAPABILITIES | STREAM_ID_NONE:

			switch (FunctionId)
			{
				case RIM_EXCHANGE_CAPABILITY_REQUEST:
					error = tsmf_ifman_rim_exchange_capability_request(&ifman);
					break;

				default:
					break;
			}
			break;

		case TSMF_INTERFACE_DEFAULT | STREAM_ID_PROXY:

			switch (FunctionId)
			{
				case SET_CHANNEL_PARAMS:
					error = tsmf_ifman_set_channel_params(&ifman);
					break;

				case EXCHANGE_CAPABILITIES_REQ:
					error = tsmf_ifman_exchange_capability_request(&ifman);
					break;

				case CHECK_FORMAT_SUPPORT_REQ:
					error = tsmf_ifman_check_format_support_request(&ifman);
					break;

				case ON_NEW_PRESENTATION:
					error = tsmf_ifman_on_new_presentation(&ifman);
					break;

				case ADD_STREAM:
					error = tsmf_ifman_add_stream(&ifman);
					break;

				case SET_TOPOLOGY_REQ:
					error = tsmf_ifman_set_topology_request(&ifman);
					break;

				case REMOVE_STREAM:
					error = tsmf_ifman_remove_stream(&ifman);
					break;

				case SHUTDOWN_PRESENTATION_REQ:
					error = tsmf_ifman_shutdown_presentation(&ifman);
					break;

				case ON_STREAM_VOLUME:
					error = tsmf_ifman_on_stream_volume(&ifman);
					break;

				case ON_CHANNEL_VOLUME:
					error = tsmf_ifman_on_channel_volume(&ifman);
					break;

				case SET_VIDEO_WINDOW:
					error = tsmf_ifman_set_video_window(&ifman);
					break;

				case UPDATE_GEOMETRY_INFO:
					error = tsmf_ifman_update_geometry_info(&ifman);
					break;

				case NOTIFY_PREROLL:
					error = tsmf_ifman_notify_preroll(&ifman);
					break;

				case ON_SAMPLE:
					error = tsmf_ifman_on_sample(&ifman);
					break;

				default:
					break;
			}
			break;

		default:
			break;
	}

	ifman.input_buffer = NULL;
	ifman.input_buffer_size = 0;

	if (error == -1)
	{
		switch (FunctionId)
		{
			case RIMCALL_RELEASE:
				/* [MS-RDPEXPS] 2.2.2.2 Interface Release (IFACE_RELEASE)
				   This message does not require a reply. */
				error = 0;
				ifman.output_pending = 1;
				break;

			case RIMCALL_QUERYINTERFACE:
				/* [MS-RDPEXPS] 2.2.2.1.2 Query Interface Response (QI_RSP)
				   This message is not supported in this channel. */
				error = 0;
				break;
		}

		if (error == -1)
		{
			LLOGLN(0, ("tsmf_on_data_received: InterfaceId 0x%X FunctionId 0x%X not processed.",
				InterfaceId, FunctionId));
			/* When a request is not implemented we return empty response indicating error */
		}
		error = 0;
	}

	if (error == 0 && !ifman.output_pending)
	{
		/* Response packet does not have FunctionId */
		out_size = 8 + ifman.output_buffer_size;
		out_data = (char *) malloc(out_size);
		memset(out_data, 0, out_size);
		SET_UINT32(out_data, 0, ifman.output_interface_id);
		SET_UINT32(out_data, 4, MessageId);
		if (ifman.output_buffer_size > 0)
			memcpy(out_data + 8, ifman.output_buffer, ifman.output_buffer_size);

		LLOGLN(10, ("tsmf_on_data_received: response size %d", out_size));
		error = callback->channel->Write(callback->channel, out_size, out_data, NULL);
		if (error)
		{
			LLOGLN(0, ("tsmf_on_data_received: response error %d", error));
		}

		free(out_data);
		if (ifman.output_buffer_size > 0)
		{
			free(ifman.output_buffer);
			ifman.output_buffer = NULL;
			ifman.output_buffer_size = 0;
		}
	}

	return error;
}

static int
tsmf_on_close(IWTSVirtualChannelCallback * pChannelCallback)
{
	LLOGLN(10, ("tsmf_on_close:"));
	free(pChannelCallback);
	return 0;
}

static int
tsmf_on_new_channel_connection(IWTSListenerCallback * pListenerCallback,
	IWTSVirtualChannel * pChannel,
	char * Data,
	int * pbAccept,
	IWTSVirtualChannelCallback ** ppCallback)
{
	TSMF_LISTENER_CALLBACK * listener_callback = (TSMF_LISTENER_CALLBACK *) pListenerCallback;
	TSMF_CHANNEL_CALLBACK * callback;

	LLOGLN(10, ("tsmf_on_new_channel_connection:"));
	callback = (TSMF_CHANNEL_CALLBACK *) malloc(sizeof(TSMF_CHANNEL_CALLBACK));
	callback->iface.OnDataReceived = tsmf_on_data_received;
	callback->iface.OnClose = tsmf_on_close;
	callback->plugin = listener_callback->plugin;
	callback->channel_mgr = listener_callback->channel_mgr;
	callback->channel = pChannel;
	*ppCallback = (IWTSVirtualChannelCallback *) callback;
	return 0;
}

static int
tsmf_plugin_initialize(IWTSPlugin * pPlugin, IWTSVirtualChannelManager * pChannelMgr)
{
	TSMF_PLUGIN * tsmf = (TSMF_PLUGIN *) pPlugin;

	LLOGLN(10, ("tsmf_plugin_initialize:"));
	tsmf->listener_callback = (TSMF_LISTENER_CALLBACK *) malloc(sizeof(TSMF_LISTENER_CALLBACK));
	memset(tsmf->listener_callback, 0, sizeof(TSMF_LISTENER_CALLBACK));

	tsmf->listener_callback->iface.OnNewChannelConnection = tsmf_on_new_channel_connection;
	tsmf->listener_callback->plugin = pPlugin;
	tsmf->listener_callback->channel_mgr = pChannelMgr;
	return pChannelMgr->CreateListener(pChannelMgr, "TSMF", 0,
		(IWTSListenerCallback *) tsmf->listener_callback, NULL);
}

static int
tsmf_plugin_terminated(IWTSPlugin * pPlugin)
{
	TSMF_PLUGIN * tsmf = (TSMF_PLUGIN *) pPlugin;

	LLOGLN(10, ("tsmf_plugin_terminated:"));
	if (tsmf->listener_callback)
		free(tsmf->listener_callback);
	free(tsmf);
	return 0;
}

int
DVCPluginEntry(IDRDYNVC_ENTRY_POINTS * pEntryPoints)
{
	TSMF_PLUGIN * tsmf;
	int ret = 0;

	tsmf = (TSMF_PLUGIN *) pEntryPoints->GetPlugin(pEntryPoints, "tsmf");
	if (tsmf == NULL)
	{
		tsmf = (TSMF_PLUGIN *) malloc(sizeof(TSMF_PLUGIN));
		memset(tsmf, 0, sizeof(TSMF_PLUGIN));

		tsmf->iface.Initialize = tsmf_plugin_initialize;
		tsmf->iface.Connected = NULL;
		tsmf->iface.Disconnected = NULL;
		tsmf->iface.Terminated = tsmf_plugin_terminated;
		ret = pEntryPoints->RegisterPlugin(pEntryPoints, "tsmf", (IWTSPlugin *) tsmf);
	}
	return ret;
}

