#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <bcm_host.h>
#include "cam.h"
#include "input.h"
#include "osd.h"
#include "telem.h"

#define VID_DIR "/mnt/mmcblk0p1/fpv/"

static int cam_start_slot(cam_t cam, unsigned int slot)
{
	char path[sizeof(VID_DIR) + 10 + 1 + 4];
	sprintf(path, VID_DIR "%06u.h264", slot);

	return cam_start(cam, path);
}

static unsigned int find_free_slot()
{
	DIR *dir;
	struct dirent *entry;
	unsigned int entry_slot, last_slot = 0;

	dir = opendir(VID_DIR);
	if(dir)
	{
		while(entry = readdir(dir))
		{
			if(sscanf(entry->d_name, "%u.h264", &entry_slot) && entry_slot > last_slot)
			{
				last_slot = entry_slot;
			}
		}
		closedir(dir);
	}
	return last_slot + 1;
}

int main(int argc, char **argv)
{
	cam_t cam = 0;
	input_t input = 0;
	osd_t osd = 0;
	telem_t telem = 0;

	const char *error = 0;
	unsigned int next_slot;

	bcm_host_init();

	cam = cam_init();
	if(!cam)
	{
		error = cam_error();
		goto cleanup;
	}

	input = input_init(7, 1);
	if(!input)
	{
		error = "Failed to initialize GPIO.";
		goto cleanup;
	}

	osd = osd_init();
	if(!osd)
	{
		error = osd_error();
		goto cleanup;
	}

	telem = telem_open("/dev/ttyAMA0");
	if(!telem)
	{
		error = telem_error();
		goto cleanup;
	}

	next_slot = find_free_slot();
	while(1)
	{
		input_update(input);
		uint16_t value = input_get(input);
		if(value > 1500)
		{
			if(!cam_recording(cam))
			{
				cam_start_slot(cam, next_slot++);
				osd_set_recording(osd, 1);
			}
		}
		else if(value > 800)
		{
			if(cam_recording(cam))
			{
				cam_stop(cam);
				osd_set_recording(osd, 0);
			}
		}

		int result = telem_update(telem);
		if(result > 0)
		{
			osd_set_altitude(osd, telem_get_altitude(telem));
			osd_set_heading(osd, telem_get_heading(telem));
			osd_set_voltage(osd, telem_get_vfas_voltage(telem), telem_get_cells(telem));
			osd_update(osd);
		}
	}
	
cleanup:
	if(telem) telem_close(telem);
	if(osd) osd_deinit(osd);
	if(cam) cam_deinit(cam);

	if(error)
	{
		fprintf(stderr, "Fatal error: %s\n", error);
		return 1;
	}

	return 0;
}
