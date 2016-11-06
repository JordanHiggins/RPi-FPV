#include "cam.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_connection.h>
#include <interface/mmal/util/mmal_default_components.h>
#include <interface/mmal/util/mmal_util.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <interface/vcos/vcos.h>

#define CAM_AWB_B_DEN 1
#define CAM_AWB_B_NUM 1
#define CAM_AWB_MODE MMAL_PARAM_AWBMODE_AUTO
#define CAM_AWB_R_DEN 1
#define CAM_AWB_R_NUM 1
#define CAM_BRIGHTNESS_DEN 2
#define CAM_BRIGHTNESS_NUM 1
#define CAM_CONTRAST_DEN 1
#define CAM_CONTRAST_NUM 0
#define CAM_DRC MMAL_PARAMETER_DRC_STRENGTH_HIGH
#define CAM_EXPOSURE_COMPENSATION 0
#define CAM_EXPOSURE_MODE MMAL_PARAM_EXPOSUREMODE_SPORTS
#define CAM_HEIGHT 1232
#define CAM_HFLIP 0
#define CAM_ISO 0
#define CAM_MODE 4
#define CAM_ROTATION 0
#define CAM_SHARPNESS_DEN 1
#define CAM_SHARPNESS_NUM 0
#define CAM_STABILIZATION 0
#define CAM_VFLIP 0
#define CAM_WIDTH 1640

#define PRE_ALPHA 255
#define PRE_FRAMERATE_DEN 1
#define PRE_FRAMERATE_NUM 0
#define PRE_LAYER 128

#define VID_BITRATE 0
#define VID_FRAMERATE_DEN 1
#define VID_FRAMERATE_NUM 30
#define VID_QUALITY 20

struct cam
{
	MMAL_COMPONENT_T *camera_component;
	MMAL_COMPONENT_T *encoder_component;
	MMAL_COMPONENT_T *preview_component;
	MMAL_COMPONENT_T *splitter_component;

	MMAL_CONNECTION_T *encoder_connection;
	MMAL_CONNECTION_T *preview_connection;
	MMAL_CONNECTION_T *splitter_connection;

	MMAL_POOL_T *output_pool;

	MMAL_PORT_T *camera_preview_port;
	MMAL_PORT_T *camera_video_port;
	MMAL_PORT_T *encoder_in_port;
	MMAL_PORT_T *encoder_out_port;
	MMAL_PORT_T *preview_in_port;

	FILE *file;
};

static const char *_error;

static void cam_deinit_camera(cam_t cam)
{
	if(cam->camera_component)
	{
		mmal_component_destroy(cam->camera_component);
		cam->camera_component = 0;
		cam->camera_preview_port = 0;
		cam->camera_video_port = 0;
	}
}

static void cam_deinit_encoder(cam_t cam)
{
	if(cam->encoder_out_port && cam->encoder_out_port->is_enabled)
	{
		mmal_port_disable(cam->encoder_out_port);
	}

	if(cam->encoder_connection)
	{
		mmal_connection_destroy(cam->encoder_connection);
		cam->encoder_connection = 0;
	}

	if(cam->output_pool)
	{
		mmal_port_pool_destroy(cam->encoder_out_port, cam->output_pool);
		cam->output_pool = 0;
	}

	if(cam->encoder_component)
	{
		mmal_component_destroy(cam->encoder_component);
		cam->encoder_component = 0;
		cam->encoder_in_port = 0;
		cam->encoder_out_port = 0;
	}
}

static void cam_deinit_preview(cam_t cam)
{
	if(cam->preview_connection)
	{
		mmal_connection_destroy(cam->preview_connection);
		cam->preview_connection = 0;
	}

	if(cam->preview_component)
	{
		mmal_component_destroy(cam->preview_component);
		cam->preview_component = 0;
		cam->preview_in_port = 0;
	}
}

static void cam_deinit_splitter(cam_t cam)
{
	if(cam->splitter_connection)
	{
		mmal_connection_destroy(cam->splitter_connection);
		cam->splitter_connection = 0;
	}

	if(cam->splitter_component)
	{
		mmal_component_destroy(cam->splitter_component);
		cam->splitter_component = 0;
	}
}

void cam_deinit(cam_t cam)
{
	cam_deinit_encoder(cam);
	cam_deinit_splitter(cam);
	cam_deinit_preview(cam);
	cam_deinit_camera(cam);

	if(cam->file) fclose(cam->file);

	free(cam);
}

static void cam_callback_encoder_out(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	cam_t cam = (cam_t)port->userdata;
	MMAL_BUFFER_HEADER_T *new_buffer;
	MMAL_STATUS_T status;

	mmal_buffer_header_mem_lock(buffer);
	fwrite(buffer->data, 1, buffer->length, cam->file);
	mmal_buffer_header_mem_unlock(buffer);
	mmal_buffer_header_release(buffer);

	if(port->is_enabled)
	{
		new_buffer = mmal_queue_get(cam->output_pool->queue);
		if(new_buffer)
		{
			status = mmal_port_send_buffer(port, new_buffer);
		}

		if(!new_buffer || status != MMAL_SUCCESS)
		{
			fprintf(stderr, "WARNING: Failed to send new buffer to encoder.\n");
			return;
		}
	}
}

const char *cam_error()
{
	return _error;
}

static int cam_init_camera(cam_t cam)
{
	MMAL_STATUS_T status;

	// Create the camera component
	{
		status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &cam->camera_component);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to create camera component.";
			goto error;
		}

		cam->camera_preview_port = cam->camera_component->output[0];
		cam->camera_video_port = cam->camera_component->output[1];
	}

	// Set basic camera configuration
	{
		MMAL_PARAMETER_CAMERA_CONFIG_T config = {
			{MMAL_PARAMETER_CAMERA_CONFIG, sizeof(config)},
			.max_stills_w = CAM_WIDTH,
			.max_stills_h = CAM_HEIGHT,
			.stills_yuv422 = 0,
			.one_shot_stills = 1,

			.max_preview_video_w = CAM_WIDTH,
			.max_preview_video_h = CAM_HEIGHT,
			.num_preview_video_frames = 3,

			.stills_capture_circular_buffer_height = 0,

			.fast_preview_resume = 1,
			.use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC,
		};

		status = mmal_port_parameter_set(cam->camera_component->control, &config.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set camera control port parameters.";
			goto error;
		}
	}

	// Set the AWB gains
	{
		MMAL_PARAMETER_AWB_GAINS_T gains = {
			{MMAL_PARAMETER_CUSTOM_AWB_GAINS, sizeof(gains)},
			{CAM_AWB_R_NUM, CAM_AWB_R_DEN},
			{CAM_AWB_B_NUM, CAM_AWB_B_DEN},
		};

		status = mmal_port_parameter_set(cam->camera_component->control, &gains.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set AWB gains.";
			goto error;
		}
	}

	// Set the AWB mode
	{
		MMAL_PARAMETER_AWBMODE_T mode = {
			{MMAL_PARAMETER_AWB_MODE, sizeof(mode)},
			CAM_AWB_MODE,
		};

		status = mmal_port_parameter_set(cam->camera_component->control, &mode.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set AWB mode.";
			goto error;
		}
	}

	// Set the brightness
	{
		MMAL_PARAMETER_RATIONAL_T brightness = {
			{MMAL_PARAMETER_BRIGHTNESS, sizeof(brightness)},
			{CAM_BRIGHTNESS_NUM, CAM_BRIGHTNESS_DEN},
		};

		status = mmal_port_parameter_set(cam->camera_component->control, &brightness.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set brightness.";
			goto error;
		}
	}

	// Set the contrast
	{
		MMAL_PARAMETER_RATIONAL_T contrast = {
			{MMAL_PARAMETER_CONTRAST, sizeof(contrast)},
			{CAM_CONTRAST_NUM, CAM_CONTRAST_DEN},
		};

		status = mmal_port_parameter_set(cam->camera_component->control, &contrast.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set contrast.";
			goto error;
		}
	}

	// Set the DRC mode
	{
		MMAL_PARAMETER_DRC_T drc = {
			{MMAL_PARAMETER_DYNAMIC_RANGE_COMPRESSION, sizeof(drc)},
			CAM_DRC,
		};

		status = mmal_port_parameter_set(cam->camera_component->control, &drc.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set DRC mode.";
			goto error;
		}
	}

	// Set the exposure compensation
	{
		status = mmal_port_parameter_set_int32(cam->camera_component->control, MMAL_PARAMETER_EXPOSURE_COMP, CAM_EXPOSURE_COMPENSATION);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set exposure compensation.";
			goto error;
		}
	}

	// Set the exposure mode
	{
		MMAL_PARAMETER_EXPOSUREMODE_T mode = {
			{MMAL_PARAMETER_EXPOSURE_MODE, sizeof(mode)},
			CAM_EXPOSURE_MODE,
		};

		status = mmal_port_parameter_set(cam->camera_component->control, &mode.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set exposure mode.";
			goto error;
		}
	}

	// Set the horizontal/vertical flips
	{
		MMAL_PARAMETER_MIRROR_T mirror = {
			{MMAL_PARAMETER_MIRROR, sizeof(mirror)},
			(CAM_HFLIP ? MMAL_PARAM_MIRROR_HORIZONTAL : 0) | (CAM_VFLIP ? MMAL_PARAM_MIRROR_VERTICAL : 0),
		};

		status = mmal_port_parameter_set(cam->camera_preview_port, &mirror.hdr);
		status += mmal_port_parameter_set(cam->camera_video_port, &mirror.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set flips.";
			goto error;
		}
	}

	// Set the ISO
	{
		status = mmal_port_parameter_set_uint32(cam->camera_component->control, MMAL_PARAMETER_ISO, CAM_ISO);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set ISO.";
			goto error;
		}
	}

	// Set the mode
	{
		status = mmal_port_parameter_set_uint32(cam->camera_component->control, MMAL_PARAMETER_CAMERA_CUSTOM_SENSOR_CONFIG, CAM_MODE);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set camera mode.";
			goto error;
		}
	}

	// Set the rotation
	{
		MMAL_PARAMETER_INT32_T rotation = {
			{MMAL_PARAMETER_ROTATION, sizeof(rotation)},
			CAM_ROTATION,
		};

		status = mmal_port_parameter_set(cam->camera_preview_port, &rotation.hdr);
		status += mmal_port_parameter_set(cam->camera_video_port, &rotation.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set rotation.";
			goto error;
		}
	}

	// Set the sharpness
	{
		MMAL_PARAMETER_RATIONAL_T sharpness = {
			{MMAL_PARAMETER_SHARPNESS, sizeof(sharpness)},
			{CAM_SHARPNESS_NUM, CAM_SHARPNESS_DEN},
		};

		status = mmal_port_parameter_set(cam->camera_component->control, &sharpness.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set sharpness.";
			goto error;
		}
	}

	// Set the stabilization
	{
		status = mmal_port_parameter_set_boolean(cam->camera_component->control, MMAL_PARAMETER_VIDEO_STABILISATION, CAM_STABILIZATION);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set stabilization.";
			goto error;
		}
	}

	// Set up preview format
	{
		cam->camera_preview_port->format->encoding = MMAL_ENCODING_OPAQUE;
		cam->camera_preview_port->format->es->video.width = VCOS_ALIGN_UP(CAM_WIDTH, 32);
		cam->camera_preview_port->format->es->video.height = VCOS_ALIGN_UP(CAM_HEIGHT, 16);
		cam->camera_preview_port->format->es->video.crop.x = 0;
		cam->camera_preview_port->format->es->video.crop.y = 0;
		cam->camera_preview_port->format->es->video.crop.width = CAM_WIDTH;
		cam->camera_preview_port->format->es->video.crop.height = CAM_HEIGHT;
		cam->camera_preview_port->format->es->video.frame_rate.den = PRE_FRAMERATE_DEN;
		cam->camera_preview_port->format->es->video.frame_rate.num = PRE_FRAMERATE_NUM;

		status = mmal_port_format_commit(cam->camera_preview_port);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set format of preview port.";
			goto error;
		}
	}

	// Set up video format
	{
		cam->camera_video_port->format->encoding = MMAL_ENCODING_OPAQUE;
		cam->camera_video_port->format->es->video.width = VCOS_ALIGN_UP(CAM_WIDTH, 32);
		cam->camera_video_port->format->es->video.height = VCOS_ALIGN_UP(CAM_HEIGHT, 16);
		cam->camera_video_port->format->es->video.crop.x = 0;
		cam->camera_video_port->format->es->video.crop.y = 0;
		cam->camera_video_port->format->es->video.crop.width = CAM_WIDTH;
		cam->camera_video_port->format->es->video.crop.height = CAM_HEIGHT;
		cam->camera_video_port->format->es->video.frame_rate.num = VID_FRAMERATE_NUM;
		cam->camera_video_port->format->es->video.frame_rate.den = VID_FRAMERATE_DEN;

		status = mmal_port_format_commit(cam->camera_video_port);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set format of video port.";
			goto error;
		}

		if(cam->camera_video_port->buffer_num < 3)
			cam->camera_video_port->buffer_num = 3;
	}

	return 0;

error:
	cam_deinit_camera(cam);
	return 1;
}

static int cam_init_encoder(cam_t cam)
{
	MMAL_STATUS_T status;

	// Create the encoder component
	{
		status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER, &cam->encoder_component);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to create encoder component.";
			goto error;
		}

		cam->encoder_in_port = cam->encoder_component->input[0];
		cam->encoder_out_port = cam->encoder_component->output[0];
	}

	// Set the encoder output format
	{
		mmal_format_copy(cam->encoder_out_port->format, cam->encoder_in_port->format);

		// Set buffer count (larger of minimum and recommended)
		cam->encoder_out_port->buffer_num = cam->encoder_out_port->buffer_num_recommended;
		if(cam->encoder_out_port->buffer_num < cam->encoder_out_port->buffer_num_min)
			cam->encoder_out_port->buffer_num = cam->encoder_out_port->buffer_num_min;

		// Set buffer size (larger of minimum and recommended)
		cam->encoder_out_port->buffer_size = cam->encoder_out_port->buffer_size_recommended;
		if(cam->encoder_out_port->buffer_size < cam->encoder_out_port->buffer_size_min)
			cam->encoder_out_port->buffer_size = cam->encoder_out_port->buffer_size_min;

		// Set other encoding properties
		cam->encoder_out_port->format->bitrate = VID_BITRATE;
		cam->encoder_out_port->format->encoding = MMAL_ENCODING_H264;
		cam->encoder_out_port->format->es->video.frame_rate.num = 0;
		cam->encoder_out_port->format->es->video.frame_rate.den = 1;

		status = mmal_port_format_commit(cam->encoder_out_port);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set encoding format.";
			goto error;
		}
	}

	// Set the encoding profile
	{
		MMAL_PARAMETER_VIDEO_PROFILE_T param = {
			{MMAL_PARAMETER_PROFILE, sizeof(param)},
			.profile = {
				{
					.level = MMAL_VIDEO_LEVEL_H264_4,
					.profile = MMAL_VIDEO_PROFILE_H264_HIGH,
				}
			},
		};

		status = mmal_port_parameter_set(cam->encoder_out_port, &param.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set encoding profile.";
			goto error;
		}
	}

	// Set the encoding quality
	{
		MMAL_PARAMETER_UINT32_T quantization = {
			{MMAL_PARAMETER_VIDEO_ENCODE_INITIAL_QUANT, sizeof(quantization)},
			VID_QUALITY,
		};

		status = mmal_port_parameter_set(cam->encoder_out_port, &quantization.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set the initial quantization.";
			goto error;
		}

		quantization.hdr.id = MMAL_PARAMETER_VIDEO_ENCODE_MIN_QUANT;
		status = mmal_port_parameter_set(cam->encoder_out_port, &quantization.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set the minimum quantization.";
			goto error;
		}

		quantization.hdr.id = MMAL_PARAMETER_VIDEO_ENCODE_MAX_QUANT;
		status = mmal_port_parameter_set(cam->encoder_out_port, &quantization.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set the maximum quantization.";
			goto error;
		}
	}

	// Enable the encoder
	{
		status = mmal_component_enable(cam->encoder_component);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to enable encoder.";
			goto error;
		}
	}

	// Create output pool
	{
		cam->output_pool = mmal_port_pool_create(cam->encoder_out_port, cam->encoder_out_port->buffer_num, cam->encoder_out_port->buffer_size);
		if(!cam->output_pool)
		{
			_error = "Failed to create output pool.";
			goto error;
		}
	}

	return 0;

error:
	cam_deinit_encoder(cam);
	return 1;
}

static int cam_init_preview(cam_t cam)
{
	MMAL_STATUS_T status;

	// Create the preview component
	{
		status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &cam->preview_component);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to create preview component.";
			goto error;
		}

		cam->preview_in_port = cam->preview_component->input[0];
	}

	// Set the display region
	{
		MMAL_DISPLAYREGION_T region = {
			{MMAL_PARAMETER_DISPLAYREGION, sizeof(region)},
			.alpha = PRE_ALPHA,
			.fullscreen = 1,
			.layer = PRE_LAYER,
			.set = MMAL_DISPLAY_SET_ALPHA | MMAL_DISPLAY_SET_FULLSCREEN | MMAL_DISPLAY_SET_LAYER,
		};

		status = mmal_port_parameter_set(cam->preview_in_port, &region.hdr);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to set preview display region.";
			goto error;
		}
	}

	// Enable the preview component
	{
		status = mmal_component_enable(cam->preview_component);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to enable preview component.";
			goto error;
		}
	}

	return 0;

error:
	cam_deinit_preview(cam);
	return 1;
}

static int cam_init_splitter(cam_t cam)
{
	MMAL_STATUS_T status;

	// Create the splitter
	{
		status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_SPLITTER, &cam->splitter_component);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to create splitter.";
			goto error;
		}
	}

	return 0;

error:
	cam_deinit_splitter(cam);
	return 1;
}

cam_t cam_init()
{
	cam_t cam = 0;
	int result;
	MMAL_STATUS_T status;

	cam = malloc(sizeof(struct cam));
	if(!cam)
	{
		_error = "Failed to allocate memory for camera object.";
		goto error;
	}

	// Create the camera
	result = cam_init_camera(cam);
	if(result) goto error;

	// Create the preview
	result = cam_init_preview(cam);
	if(result) goto error;

	// Create and enable the connection from the camera to the preview
	{
		status = mmal_connection_create(&cam->preview_connection, cam->camera_preview_port, cam->preview_in_port, MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT | MMAL_CONNECTION_FLAG_TUNNELLING);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to create connection from camera to preview.";
			goto error;
		}

		status = mmal_connection_enable(cam->preview_connection);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to enable connection from camera to preview.";
			goto error;
		}
	}

	// Create the splitter
	result = cam_init_splitter(cam);
	if(result) goto error;

	// Create and enable the connection from the camera to the splitter
	{
		status = mmal_connection_create(&cam->splitter_connection, cam->camera_video_port, cam->splitter_component->input[0], MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT | MMAL_CONNECTION_FLAG_TUNNELLING);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to create connection from camera to splitter.";
			goto error;
		}

		status = mmal_connection_enable(cam->splitter_connection);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to enable connection from camera to splitter.";
			goto error;
		}
	}

	// Set the splitter output formats
	{
		int i;
		for(i = 0; i < 4; i++)
		{
			mmal_format_copy(cam->splitter_component->output[i]->format, cam->splitter_component->input[0]->format);
	
			status = mmal_port_format_commit(cam->splitter_component->output[i]);
			if(status != MMAL_SUCCESS)
			{
				_error = "Failed to set format on encoder output port.";
				goto error;
			}
		}
	}
	
	// Create the encoder
	result = cam_init_encoder(cam);
	if(result) goto error;

	// Create the connection from the splitter to the encoder
	{
		status = mmal_connection_create(&cam->encoder_connection, cam->splitter_component->output[0], cam->encoder_in_port, MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT | MMAL_CONNECTION_FLAG_TUNNELLING);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to create connection from splitter to encoder.";
			goto error;
		}
	}

	return cam;

error:
	if(cam) cam_deinit(cam);
	return 0;
}

int cam_recording(cam_t cam)
{
	return cam->file != 0;
}

int cam_start(cam_t cam, const char *path)
{
	MMAL_STATUS_T status;

	if(cam_recording(cam))
	{
		_error = "Camera is already recording.";
		return 1;
	}

	// Open the file
	{
		cam->file = fopen(path, "wb");
		if(!cam->file)
		{
			_error = "Failed to open output file.";
			goto error;
		}
	}

	// Enable the connection from the camera to the encoder
	{
		status = mmal_connection_enable(cam->encoder_connection);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to enable connection from camera to encoder.";
			goto error;
		}
	}

	// Enable the encoder's output port
	{
		cam->encoder_out_port->userdata = (struct MMAL_PORT_USERDATA_T *)cam;		

		status = mmal_port_enable(cam->encoder_out_port, &cam_callback_encoder_out);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to enable encoder output port.";
			return 1;
		}
	}

	// Request a keyframe
	{
		status = mmal_port_parameter_set_boolean(cam->encoder_out_port, MMAL_PARAMETER_VIDEO_REQUEST_I_FRAME, 1);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to request a keyframe.";
			goto error;
		}
	}

	// Feed the encoder's output port
	{
		MMAL_BUFFER_HEADER_T *buffer;
		while(buffer = mmal_queue_get(cam->output_pool->queue))
		{
			status = mmal_port_send_buffer(cam->encoder_out_port, buffer);
			if(status != MMAL_SUCCESS)
			{
				_error = "Failed to send buffer to encoder input port.";
				return 1;
			}
		}
	}

	// Start capturing on the camera video port
	{
		status = mmal_port_parameter_set_boolean(cam->camera_video_port, MMAL_PARAMETER_CAPTURE, 1);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to start capturing on the camera video port.";
			goto error;
		}
	}

	return 0;

error:
	if(cam->encoder_out_port->is_enabled)
	{
		mmal_port_disable(cam->encoder_out_port);
	}

	if(cam->file)
	{
		fclose(cam->file);
		cam->file = 0;
	}

	return 1;
}

int cam_stop(cam_t cam)
{
	MMAL_STATUS_T status;

	if(!cam_recording(cam))
	{
		_error = "Camera is not recording.";
		return 1;
	}

	// Stop capturing on the camera video port
	{
		status = mmal_port_parameter_set_boolean(cam->camera_video_port, MMAL_PARAMETER_CAPTURE, 0);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to stop capturing on the camera video port.";
			return 1;
		}
	}

	// Disable the connection from splitter to encoder
	{
		status = mmal_connection_disable(cam->encoder_connection);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to disable connection from splitter to encoder.";
			return 1;
		}
	}

	// Disable the encoder's output port
	{
		status = mmal_port_disable(cam->encoder_out_port);
		if(status != MMAL_SUCCESS)
		{
			_error = "Failed to disable encoder output port.";
			return 1;
		}
	}

	// Wait for encoder to flush
	{
		unsigned int expected = cam->encoder_out_port->buffer_num;
		while(mmal_queue_length(cam->output_pool->queue) < expected);
	}

	// Close the file
	{
		fclose(cam->file);
		cam->file = 0;
	}

	return 0;
}