/*
 * This is an example application that demonstrates how to use the axptz library.
 * It does not contain examples that cover all of the axptz library functions,
 * the purpose of this example application is to give the end user some basic
 * knowledge on how to use the library.
 */

#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <fixmath.h>
#include <axsdk/axptz.h>
#include <axsdk/axparameter.h>
#include <licensekey.h>

/* This activates logging to syslog */
#define WRITE_TO_SYS_LOG

#ifdef WRITE_TO_SYS_LOG
#define LOGINFO(fmt, args...) \
  do { \
    syslog(LOG_INFO, fmt, ## args); \
    printf(fmt, ## args); \
    printf("\n"); \
  } while(0)
#else
#define LOGINFO(fmt, args...)
#endif

#define APP_NAME "panoramatv"

//#define REQUIRE_LICENSE

#define CUSTOM_LICENSE_VALID    "Valid"
#define CUSTOM_LICENSE_INVALID  "Invalid"
#define CUSTOM_LICENSE_MISSING  "Missing"
#define CUSTOM_LICENSE_NONE     "None"

#define AXIS_LICENSE_CHECK_OK 1
#define AXIS_LICENSE_CHECK_NOT_OK 0
#define APP_ID 256105
#define MAJOR_VERSION 1
#define MINOR_VERSION 0

/* The number of fractional bits used in fix-point variables */
#define FIXMATH_FRAC_BITS 16

#define VIDEO_CHANNEL 1

#define MAX_PAN_TILT_SPEED 0.5
#define MIN_PAN_TILT_SPEED 0.1
#define MAX_PRESET_NUMBER 20

#define NPT 2 //the number of points between the presets

#define SLEEP_TIME_MILLISECONDS 100

typedef struct PTZ_POS{
  
	fixed_t pan_val;
	fixed_t tilt_val;
	fixed_t zoom_val;
  
}PTZ_POS;

/* global variables */
static AXPTZControlQueueGroup *ax_ptz_control_queue_group = NULL;
static GList *capabilities = NULL;
static AXPTZStatus *unitless_status = NULL;

static fixed_t fx_zero = fx_itox(0, FIXMATH_FRAC_BITS);
static fixed_t fx_two = fx_ftox(2.0f, FIXMATH_FRAC_BITS);
static fixed_t fx_four = fx_ftox(4.0f, FIXMATH_FRAC_BITS);

static fixed_t arrival_accuracy = fx_ftox(0.002f, FIXMATH_FRAC_BITS);

static gfloat cont_max_speed = 0.3f;//max pan_tilt_speed
static gint stop_in_preset = 0;//stop in preset for 1 sec

static gint queue_pos = -1;
static gint time_to_pos_one = -1;
static gint poll_time = -1;
/* camera information
 * Pan Max 180 
 * Pan Min -180
 * Pan Speed Max 700
 * Pan Speed Min 0.05
 * Tilt Max 20
 * Tilt Min -90
 * Tilt Speed Max 500
 * Tilt Speed Min 0.05
 * Zoom Speed Max 500
 * Zoom Max 24x
 * Zoom Min 1x
 */



static void logCameraInfo(char *fmt)
{
    syslog(LOG_INFO, fmt); 
    printf(fmt); 
    printf("\n"); 
}
/*
 * Get the camera pan/tilt speed from vapix
 */
//static gfloat

/*
 * Get the camera's supported PTZ move capabilities
 */
static gboolean get_ptz_move_capabilities(void)
{
	GError *local_error = NULL;

	if (!(capabilities = ax_ptz_movement_handler_get_move_capabilities(VIDEO_CHANNEL, &local_error))) 
	{
		g_error_free(local_error);
		return FALSE;
	}
	LOGINFO("GETTING capabilities");
	GList *it = NULL;
	for (it = g_list_first(capabilities); it != NULL; it = g_list_next(it)) 
	{
		LOGINFO((gchar *) it->data);
	}
	return TRUE;
}

/*
 * Check if a capability is supported
 */
static gboolean is_capability_supported(const char *capability)
{
	gboolean is_supported = FALSE;
	GList *it = NULL;

	if (capabilities) 
	{
		for (it = g_list_first(capabilities); it != NULL; it = g_list_next(it)) 
		{
			if (!(g_strcmp0((gchar *) it->data, capability))) 
			{
			is_supported = TRUE;
			break;
			}
		}
	}

	return is_supported;
}

/*
 * Wait for the camera to reach it's position
 */
static gboolean wait_for_camera_movement_to_finish()
{
	gboolean is_moving = TRUE;
	gushort timer = 0;
	gushort timeout = 5000;
	GError *local_error = NULL;

	/* Check if camera is moving */
	if (!(ax_ptz_movement_handler_is_ptz_moving(VIDEO_CHANNEL, &is_moving, &local_error))) 
	{
		g_error_free(local_error);
		return FALSE;
	}

	/* Wait until camera is in position or until we get a timeout */
	while (is_moving && (timer < timeout)) 
	{
		if (!(ax_ptz_movement_handler_is_ptz_moving(VIDEO_CHANNEL, &is_moving, &local_error))) 
		{
			LOGINFO(local_error->message);
			g_error_free(local_error);
			return FALSE;
		}

		usleep(SLEEP_TIME_MILLISECONDS * 1000);

		timer++;
	}

	if (is_moving) 
	{
		/* Camera is still moving */
		LOGINFO("WAITING FOR CAMERA MOVEMENT TO FINISH TIME OUT");
		return FALSE;
	} 
	else 
	{
		/* Camera is in position */
		return TRUE;
	}
}

/*
 * Perform camera movement to absolute position
 */
static gboolean move_to_absolute_position(fixed_t pan_value, fixed_t tilt_value, AXPTZMovementPanTiltSpace pan_tilt_space, gfloat speed, AXPTZMovementPanTiltSpeedSpace pan_tilt_speed_space, fixed_t zoom_value, AXPTZMovementZoomSpace zoom_space)
{
	AXPTZAbsoluteMovement *abs_movement = NULL;
	GError *local_error = NULL;

	/* Set the unit spaces for an absolute movement */
	if ((ax_ptz_movement_handler_set_absolute_spaces(pan_tilt_space, pan_tilt_speed_space, zoom_space, &local_error))) 
	{
		/* Create an absolute movement structure */
		if ((abs_movement = ax_ptz_absolute_movement_create(&local_error))) 
		{

			/* Set the pan, tilt and zoom values for the absolute movement */
			if (!(ax_ptz_absolute_movement_set_pan_tilt_zoom(abs_movement, pan_value, tilt_value, fx_ftox(speed, FIXMATH_FRAC_BITS), zoom_value, AX_PTZ_MOVEMENT_NO_VALUE, &local_error))) 
			{
				ax_ptz_absolute_movement_destroy(abs_movement, NULL);
				LOGINFO(local_error->message);
				g_error_free(local_error);
				return FALSE;
			}

			/* Perform the absolute movement */
			if (!(ax_ptz_movement_handler_absolute_move(ax_ptz_control_queue_group, VIDEO_CHANNEL, abs_movement, AX_PTZ_INVOKE_ASYNC, NULL, NULL, &local_error))) 
			{	
				ax_ptz_absolute_movement_destroy(abs_movement, NULL);
				LOGINFO(local_error->message);
				g_error_free(local_error);
				return FALSE;
			}

			/* Now we don't need the absolute movement structure anymore, destroy it */
			if (!(ax_ptz_absolute_movement_destroy(abs_movement, &local_error))) 
			{
				LOGINFO(local_error->message);
				g_error_free(local_error);
				return FALSE;
			}
		} 
		else 
		{
			LOGINFO(local_error->message);
			g_error_free(local_error);
			return FALSE;
		}
	}
	else 
	{
		LOGINFO(local_error->message);
		g_error_free(local_error);
		return FALSE;
	}

	return TRUE;
}

/*
 * Perform camera movement to relative position
 */
static gboolean move_to_relative_position(fixed_t pan_value, fixed_t tilt_value, AXPTZMovementPanTiltSpace pan_tilt_space, gfloat speed, AXPTZMovementPanTiltSpeedSpace pan_tilt_speed_space, fixed_t zoom_value, AXPTZMovementZoomSpace zoom_space)
{
	AXPTZRelativeMovement *rel_movement = NULL;
	GError *local_error = NULL;

	/* Set the unit spaces for a relative movement */
	if ((ax_ptz_movement_handler_set_relative_spaces(pan_tilt_space, pan_tilt_speed_space, zoom_space, &local_error))) 
	{

		/* Create a relative movement structure */
		if ((rel_movement = ax_ptz_relative_movement_create(&local_error))) 
		{	

			/* Set the pan, tilt and zoom values for the relative movement */
			if (!(ax_ptz_relative_movement_set_pan_tilt_zoom(rel_movement, pan_value, tilt_value, fx_ftox(speed, FIXMATH_FRAC_BITS), zoom_value, AX_PTZ_MOVEMENT_NO_VALUE, &local_error))) 
			{
				ax_ptz_relative_movement_destroy(rel_movement, NULL);
				g_error_free(local_error);
				return FALSE;
			}

			/* Perform the relative movement */
			if (!(ax_ptz_movement_handler_relative_move(ax_ptz_control_queue_group, VIDEO_CHANNEL, rel_movement, AX_PTZ_INVOKE_ASYNC, NULL, NULL, &local_error))) 
			{
				ax_ptz_relative_movement_destroy(rel_movement, NULL);
				g_error_free(local_error);
				return FALSE;
			}

			/* Now we don't need the relative movement structure anymore, destroy it */
			if (!(ax_ptz_relative_movement_destroy(rel_movement, &local_error))) 
			{
				g_error_free(local_error);
				return FALSE;
			}
		} 
		else 
		{
			g_error_free(local_error);
			return FALSE;
		}
	} 
	else 
	{
		g_error_free(local_error);
		return FALSE;
	}

	return TRUE;
}

/*
 * Perform continous camera movement
 */
static gboolean start_continous_movement(fixed_t pan_speed, fixed_t tilt_speed, AXPTZMovementPanTiltSpeedSpace pan_tilt_speed_space, fixed_t zoom_speed, gfloat timeout)
{
	AXPTZContinuousMovement *cont_movement = NULL;
	GError *local_error = NULL;

	/* Set the unit spaces for a continous movement */
	if ((ax_ptz_movement_handler_set_continuous_spaces(pan_tilt_speed_space, &local_error))) 
	{

		/* Create a continous movement structure */
		if ((cont_movement = ax_ptz_continuous_movement_create(&local_error))) 
		{

			/* Set the pan, tilt and zoom speeds for the continous movement */
			if (!(ax_ptz_continuous_movement_set_pan_tilt_zoom(cont_movement, pan_speed, tilt_speed, zoom_speed, fx_ftox(timeout, FIXMATH_FRAC_BITS), &local_error))) 
			{
				ax_ptz_continuous_movement_destroy(cont_movement, NULL);
				LOGINFO("SETPANTILTZOOMERR");
				LOGINFO(local_error->message);
				g_error_free(local_error);
				return FALSE;
			} 

			/* Perform the continous movement */
			if (!(ax_ptz_movement_handler_continuous_start(ax_ptz_control_queue_group, VIDEO_CHANNEL, cont_movement, AX_PTZ_INVOKE_ASYNC, NULL, NULL, &local_error))) 
			{
				ax_ptz_continuous_movement_destroy(cont_movement, NULL);
				LOGINFO("STARTERR");
				LOGINFO(local_error->message);
				g_error_free(local_error);
				return FALSE;
			}

			/* Now we don't need the continous movement structure anymore, destroy it */
			if (!(ax_ptz_continuous_movement_destroy(cont_movement, &local_error))) 
			{
				LOGINFO("DESTROYERR");
				LOGINFO(local_error->message);
				g_error_free(local_error);
				return FALSE;
			}
		} 
		else 
		{
			LOGINFO("CREATEERR");
			LOGINFO(local_error->message);
			g_error_free(local_error);
			return FALSE;
		}
	} 
	else 
	{
		LOGINFO("SETSPACEERR");
		LOGINFO(local_error->message);
		g_error_free(local_error);
		return FALSE;
	}

	return TRUE;
}

/*
 * Stop continous camera movement
 */
static gboolean stop_continous_movement(gboolean stop_pan_tilt, gboolean stop_zoom)
{
	GError *local_error = NULL;

	/* Stop the continous movement */
	if (!(ax_ptz_movement_handler_continuous_stop(ax_ptz_control_queue_group, VIDEO_CHANNEL, stop_pan_tilt, stop_zoom, AX_PTZ_INVOKE_ASYNC, NULL, NULL, &local_error))) 
	{
		LOGINFO(local_error->message);
		LOGINFO("CAN NOT STOP CONTINUOUS MOVEMENT");
		g_error_free(local_error);
		return FALSE;
	}

	return TRUE;
}

//static gfloat arrival_accuracy = 0.001f;

static gboolean is_arrived_at_specific_pan_pos(fixed_t pan_val , fixed_t pan_speed)
{
	AXPTZStatus* ptz_status = NULL;
	GError* local_error = NULL;
	if (!(ax_ptz_movement_handler_get_ptz_status(VIDEO_CHANNEL, AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS, AX_PTZ_MOVEMENT_ZOOM_UNITLESS, &ptz_status, &local_error))) 
	{
		g_free(ptz_status);
		LOGINFO(local_error->message);
		g_error_free(local_error);
		return FALSE;
	}
	LOGINFO("ARRIVALCHECK PAN status : %d dest : %d , current pan speed : %d" , ptz_status->pan_value , pan_val , pan_speed) ;
	if(pan_speed > 0)
	{
		//arrival accuracy = pan_speed / 10(time interval 100ms)

		if(ptz_status->pan_value >= fx_subx(pan_val , 200/*fx_mulx(pan_speed, fx_ftox(0.0514f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)*/))// 360 / 700 * 0.1
			return TRUE;
	}
	else
	{
		if(ptz_status->pan_value <= fx_addx(pan_val , 200/*fx_mulx(pan_speed, fx_ftox(-0.0514f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)*/))
			return TRUE;
	}
	g_free(ptz_status);
	return FALSE;
}

static gboolean is_arrived_at_specific_tilt_pos(fixed_t tilt_val , fixed_t tilt_speed)
{
	AXPTZStatus* ptz_status = NULL;
	GError* local_error = NULL;
	if (!(ax_ptz_movement_handler_get_ptz_status(VIDEO_CHANNEL, AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS, AX_PTZ_MOVEMENT_ZOOM_UNITLESS, &ptz_status, &local_error))) 
	{
		g_free(ptz_status);
		LOGINFO(local_error->message);
		g_error_free(local_error);
		return FALSE;
	}
	LOGINFO("ARRIVALCHECK TILT status : %d dest : %d, current tilt speed : %d" , ptz_status->tilt_value , tilt_val , tilt_speed) ;
	if(tilt_speed > 0)
	{

		if(ptz_status->tilt_value >= fx_subx(tilt_val , 200/*fx_mulx(tilt_speed, fx_ftox(0.072f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)*/))// 360 / 500 * 0.1
			return TRUE;
	}
	else
	{
		if(ptz_status->tilt_value <= fx_addx(tilt_val , 200/*fx_mulx(tilt_speed, fx_ftox(-0.072f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)*/))
			return TRUE;
	}
	g_free(ptz_status);
	return FALSE;
}

static gboolean is_arrived_at_specific_zoom_pos(fixed_t zoom_val , fixed_t zoom_speed)
{
	AXPTZStatus* ptz_status = NULL;
	GError* local_error = NULL;
	if (!(ax_ptz_movement_handler_get_ptz_status(VIDEO_CHANNEL, AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS, AX_PTZ_MOVEMENT_ZOOM_UNITLESS, &ptz_status, &local_error))) 
	{
		g_free(ptz_status);
		LOGINFO(local_error->message);
		g_error_free(local_error);
		return FALSE;
	}
	LOGINFO("ARRIVALCHECK ZOOM status : %d dest : %d, current zoom speed : %d" , ptz_status->zoom_value , zoom_val , zoom_speed) ;
	if(zoom_speed > 0)
	{
		if(ptz_status->zoom_value >= fx_subx(zoom_val , fx_mulx(zoom_speed, fx_ftox(0.05f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)))
			return TRUE;
	}
	else
	{

		if(ptz_status->zoom_value <= fx_addx(zoom_val , fx_mulx(zoom_speed, fx_ftox(-0.05f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)))
			return TRUE;
	}
	g_free(ptz_status);
	return FALSE;
}

static gboolean is_arrived_at_specific_pos(fixed_t pan_val , fixed_t tilt_val , fixed_t pan_speed , fixed_t tilt_speed)
{
	AXPTZStatus* ptz_status = NULL;
	GError* local_error = NULL;
	if (!(ax_ptz_movement_handler_get_ptz_status(VIDEO_CHANNEL, AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS, AX_PTZ_MOVEMENT_ZOOM_UNITLESS, &ptz_status, &local_error))) 
	{
		g_free(ptz_status);
		LOGINFO(local_error->message);
		g_error_free(local_error);
		return FALSE;
	}
	LOGINFO("ARRIVALCHECK PAN status : %d dest : %d , TITL status : %d dest : %d" , ptz_status->pan_value , pan_val , ptz_status->tilt_value , tilt_val) ;
	if(pan_speed > 0)
	{
		if(ptz_status->pan_value >= fx_subx(pan_val , fx_mulx(pan_speed, fx_ftox(0.0514f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)))
			return TRUE;
	}
	else
	{
		if(ptz_status->pan_value <= fx_addx(pan_val , fx_mulx(pan_speed, fx_ftox(-0.0514f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)))
			return TRUE;
	}
	if(tilt_speed > 0)
	{
		if(ptz_status->tilt_value >= fx_subx(tilt_val , fx_mulx(tilt_speed, fx_ftox(0.072f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)))
			return TRUE;
	}
	else
	{
		if(ptz_status->tilt_value <= fx_addx(tilt_val , fx_mulx(tilt_speed, fx_ftox(-0.072f, FIXMATH_FRAC_BITS), FIXMATH_FRAC_BITS)))
			return TRUE;
	}
	g_free(ptz_status);
	return FALSE;
}

static gboolean wait_for_camera_arrive_to_specific_pos(fixed_t pan_val , fixed_t tilt_val , fixed_t zoom_val , fixed_t pan_speed , fixed_t tilt_speed , fixed_t zoom_speed)
{
	gint timer = 0;
	gboolean pan_tilt_arrived = FALSE;
	gboolean pan_arrived = FALSE;
	gboolean tilt_arrived = FALSE;
	gboolean zoom_arrived = FALSE;
	gboolean panStopped = FALSE;
	gboolean tiltStopped = FALSE;
	gboolean zoomStopped = FALSE;
	if(zoom_speed == 0)
		zoom_arrived = TRUE;
	if(pan_speed == 0)
		pan_arrived = TRUE;
	if(tilt_speed == 0)
		tilt_arrived = TRUE;
	while(!pan_arrived || !tilt_arrived || !zoom_arrived)
	{    
		timer ++;
		if(pan_speed != 0 && !pan_arrived)
			pan_arrived = is_arrived_at_specific_pan_pos(pan_val , pan_speed);
		
		if(tilt_speed != 0 && !tilt_arrived)
			tilt_arrived = is_arrived_at_specific_tilt_pos(tilt_val , tilt_speed);
		
		if(zoom_speed != 0 && !zoom_arrived)
			zoom_arrived = is_arrived_at_specific_zoom_pos(zoom_val , zoom_speed); 
		
		if(pan_arrived && !panStopped)
		{
			//if(!stop_continous_movement(TRUE , FALSE))
			//LOGINFO("CAN NOT STOP PAN_TILT_MOVEMENT");
			stop_continous_movement(TRUE , TRUE);
			pan_speed = 0;
			tilt_speed = ((tiltStopped)?0:fx_ftox((tilt_speed>0)?cont_max_speed*1.4:-cont_max_speed*1.4, FIXMATH_FRAC_BITS));
			if(tilt_speed == 0)
				zoom_speed = ((zoomStopped)?0:fx_ftox((zoom_speed>0)?cont_max_speed*1.4:-cont_max_speed*1.4, FIXMATH_FRAC_BITS));
			else
				zoom_speed = ((zoomStopped)?0:zoom_speed);
			start_continous_movement(pan_speed , tilt_speed , AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS , zoom_speed , 600.0f);
			
			panStopped = TRUE;
		}
		
		if(tilt_arrived && !tiltStopped)
		{
			//if(!stop_continous_movement(TRUE , FALSE))
			//LOGINFO("CAN NOT STOP PAN_TILT_MOVEMENT");
			stop_continous_movement(TRUE , TRUE);
			pan_speed = ((panStopped)?0:fx_ftox((pan_speed>0)?cont_max_speed:-1 * cont_max_speed , FIXMATH_FRAC_BITS));
			tilt_speed = 0;
			if(pan_speed == 0)
				zoom_speed = ((zoomStopped)?0:fx_ftox((zoom_speed>0)?cont_max_speed*1.4:-cont_max_speed*1.4, FIXMATH_FRAC_BITS));
			else
				zoom_speed = ((zoomStopped)?0:zoom_speed);
			start_continous_movement(pan_speed , tilt_speed , AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS , zoom_speed , 600.0f);
			tiltStopped = TRUE;
		}
		
		if(zoom_arrived && !zoomStopped)
		{
			//if(!stop_continous_movement(FALSE , TRUE))
			//LOGINFO("CAN NOT STOP ZOOM_MOVEMENT");
			stop_continous_movement(TRUE , TRUE);
			pan_speed = ((panStopped)?0:pan_speed);
			tilt_speed = ((tiltStopped)?0:tilt_speed);
			zoom_speed = 0;
			start_continous_movement(pan_speed , tilt_speed , AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS , zoom_speed , 600.0f);
			zoomStopped = TRUE;
		}
		usleep(SLEEP_TIME_MILLISECONDS * 1000);
	}
	LOGINFO("GETTING CLOSER IS STOPPED");

	//GError *local_error = NULL;
  
	//if(!wait_for_camera_movement_to_finish())
	//{
	//  LOGINFO("NOT ARRVIED AT pan:%d tilt:%d zoom:%d" , pan_val , tilt_val , zoom_val);
	//  return FALSE;
	//}
	//else
	//{
	LOGINFO("ARRVIED AT pan:%d tilt:%d zoom:%d" , pan_val , tilt_val , zoom_val);
	return TRUE;
	//}

}

static GList* tempPath = NULL;
static GList* realPath = NULL;

static  gint preset_numbers[MAX_PRESET_NUMBER + 1];
static  gint preset_indices[MAX_PRESET_NUMBER + 1];
static  gint preset_delay[MAX_PRESET_NUMBER + 1];
static  gint preset_count = 0;
static void get_path()
{
	preset_count = 0;
	GError *local_error = NULL;
	GList *temp = NULL;
	temp = ax_ptz_preset_handler_query_presets(ax_ptz_control_queue_group, VIDEO_CHANNEL, FALSE, &local_error);//preset names
	GList* it = NULL;

	for(it = g_list_first(temp) ; it != NULL ; it = g_list_next(it))
	{

		if(preset_count < MAX_PRESET_NUMBER)
		{
			gchar* preset_name = (gchar*)it->data;

			gint len = strlen(preset_name);
			gint k;
			if(len < 17 || len > 30)
			{
				continue;
			}
			else
			{
				gchar temp[30];
				strcpy(temp, "=");
				strcat(temp, preset_name);
				gchar *pch;
				pch = strtok(temp,"=_");
				if(pch != NULL)
				{
					if(strcmp(pch, "presetposno1") == 0)//home preset position
						continue;
				}
				else
				{
					continue;
				}
				gint len = strlen(pch);
				gchar preset_index[5];
				if (len > 11 && len < 15)
				{
				
					gint ii = 11;
					for (ii = 11; ii < len; ii++)
					{
						preset_index[ii - 11] = pch[ii];
					}
					preset_index[len - 11] = '\0';
				  
				}
				else
				{
					continue;
				}
				preset_indices[preset_count] = (gint)atoi(preset_index);
				pch = strtok(NULL, "=_");
				if(pch != NULL)
				{
					preset_numbers[preset_count] = (gint)atoi(pch);
				}
				else
				{
					continue;
				}
				pch = strtok(NULL, "=_");
				if(pch != NULL)
				{
					preset_delay[preset_count] = (gint)atoi(pch);
				}
			}

			LOGINFO("PRESETNUMBER%d-%d-%d-%s" , preset_count , preset_indices[preset_count] , (gint)(preset_numbers[preset_count]), preset_name);

			preset_count ++;
		}
	}
//home preset is the first one and we dont need it
//i should start from 1
  
  //sort preset_index and preset_numbers
	gint i = 0;
	for(i = 0 ; i < (preset_count - 1) ; i++)
	{
		gint j = i + 1;
		for(j = i + 1 ; j < (preset_count) ; j++)
		{
			if(preset_numbers[i] > preset_numbers[j])
			{
				gint char_temp;
				char_temp = preset_numbers[i];
				preset_numbers[i] = preset_numbers[j];
				preset_numbers[j] = char_temp;
				
				gint int_temp;
				int_temp = preset_indices[i];
				preset_indices[i] = preset_indices[j];
				preset_indices[j] = int_temp;
				
				gint int_temp1;
				int_temp1 = preset_delay[i];
				preset_delay[i] = preset_delay[j];
				preset_delay[j] = int_temp1;
			}
		}
	}
  
	if(temp != NULL)
	{
		it = NULL;
		for(it = g_list_first(temp) ; it != NULL ; it = g_list_next(it))
		{
			g_free((char*)it->data);
		}
		g_list_free(temp);
	}
	if( local_error != NULL ) g_error_free(local_error);
	return;
}


static void get_circular_path()
{
	GList* it = NULL;
	gint pathCount = 0;

	for(it = g_list_first(tempPath) ; it != NULL ; it = g_list_next(it))
	{
		pathCount ++;
		PTZ_POS* temp = g_malloc(sizeof(PTZ_POS));
		temp->pan_val = ((PTZ_POS*)(it->data))->pan_val;
		temp->tilt_val = ((PTZ_POS*)(it->data))->tilt_val;
		temp->zoom_val = ((PTZ_POS*)(it->data))->zoom_val;
		realPath = g_list_append(realPath , temp);
		LOGINFO("Path Number:%d" , pathCount);
		LOGINFO("Path Number:%d , PAN:%d , TILT:%d , ZOOM:%d" , pathCount , ((PTZ_POS*)(it->data))->pan_val , ((PTZ_POS*)(it->data))->tilt_val , ((PTZ_POS*)(it->data))->zoom_val);
		if(g_list_next(it) != NULL)//This means <it> is not the last node
		{
			gint i;
			for(i = 0 ; i < NPT ; i ++)
			{
				PTZ_POS* temp = g_malloc(sizeof(PTZ_POS));
				temp->pan_val = fx_addx(((PTZ_POS*)(it->data))->pan_val , fx_mulx(fx_divx(fx_subx(((PTZ_POS*)(g_list_next(it)->data))->pan_val , ((PTZ_POS*)(it->data))->pan_val) , fx_itox(NPT + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS) , fx_itox(i + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS));
				temp->tilt_val = fx_addx(((PTZ_POS*)(it->data))->tilt_val , fx_mulx(fx_divx(fx_subx(((PTZ_POS*)(g_list_next(it)->data))->tilt_val , ((PTZ_POS*)(it->data))->tilt_val) , fx_itox(NPT + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS) , fx_itox(i + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS));
				temp->zoom_val = fx_addx(((PTZ_POS*)(it->data))->zoom_val , fx_mulx(fx_divx(fx_subx(((PTZ_POS*)(g_list_next(it)->data))->zoom_val , ((PTZ_POS*)(it->data))->zoom_val) , fx_itox(NPT + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS) , fx_itox(i + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS));
				realPath = g_list_append(realPath , temp);
				pathCount ++;
				LOGINFO("Path Number:%d" , g_list_length(realPath));
				LOGINFO("Path Number:%d , PAN:%d , TILT:%d , ZOOM:%d" , pathCount , temp->pan_val , temp->tilt_val , temp->zoom_val);
			}
		}
		else//This means <it> is the last node
		{
			gint i;
			for(i = 0 ; i < NPT ; i ++)
			{
				PTZ_POS* temp = g_malloc(sizeof(PTZ_POS));
				temp->pan_val = fx_addx(((PTZ_POS*)(it->data))->pan_val , fx_mulx(fx_divx(fx_subx(((PTZ_POS*)(g_list_first(tempPath)->data))->pan_val , ((PTZ_POS*)(it->data))->pan_val) , fx_itox(NPT + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS) , fx_itox(i + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS));
				temp->tilt_val = fx_addx(((PTZ_POS*)(it->data))->tilt_val , fx_mulx(fx_divx(fx_subx(((PTZ_POS*)(g_list_first(tempPath)->data))->tilt_val , ((PTZ_POS*)(it->data))->tilt_val) , fx_itox(NPT + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS) , fx_itox(i + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS));
				temp->zoom_val = fx_addx(((PTZ_POS*)(it->data))->zoom_val , fx_mulx(fx_divx(fx_subx(((PTZ_POS*)(g_list_first(tempPath)->data))->zoom_val , ((PTZ_POS*)(it->data))->zoom_val) , fx_itox(NPT + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS) , fx_itox(i + 1 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS));
				realPath = g_list_append(realPath , temp);
				pathCount ++;
				LOGINFO("Path Number:%d" , g_list_length(realPath));
				LOGINFO("Path Number:%d , PAN:%d , TILT:%d , ZOOM:%d" , pathCount , temp->pan_val , temp->tilt_val , temp->zoom_val);
			}
		}
	}
}

/*
 * Main
 */

int main(int argc, char **argv)
{
	GError *local_error = NULL;
	GList *it = NULL;
  
#ifdef WRITE_TO_SYS_LOG
	openlog(APP_NAME, LOG_PID | LOG_CONS, LOG_USER);
#endif

#ifdef REQUIRE_LICENSE
	gint ret;
	gchar *application_name;
	ret = licensekey_verify(APP_NAME, APP_ID, MAJOR_VERSION, MINOR_VERSION);
	LOGINFO("LICENSEKEYVERIFY RESULT%d" , ret);
	if (ret == AXIS_LICENSE_CHECK_OK ) {
		printf("%s\n", CUSTOM_LICENSE_VALID);
		LOGINFO("%s: License verification succeeded\n", application_name);
	}
	else {
		printf("%s\n", CUSTOM_LICENSE_INVALID);
		LOGINFO("%s: License verification failed\n", application_name);
		goto failure;
	}
	g_free(application_name);
#endif

	LOGINFO("panoramatv started...\n");
	
	/* Get the value of the parameter "MaxPanTiltSpeed", "MaxZoomSpeed" */
	AXParameter* param = NULL;
	gchar *value = NULL;
	param = ax_parameter_new(APP_NAME , &local_error);
	if(param == NULL)
	{
		LOGINFO("Create Parameter Error!");
		goto failure;
	}
  
	if (!ax_parameter_get(param, "MaxPanTiltSpeed", &value, &local_error)) {
		goto failure;
	}
	syslog(LOG_INFO, "The value of \"MaxPanTiltSpeed\" is \"%s\"", value);
	cont_max_speed = (gfloat)(atof(value));
	if(cont_max_speed > MAX_PAN_TILT_SPEED)
	{
		LOGINFO("max pan tilt speed is bigger than %f", MAX_PAN_TILT_SPEED);
		cont_max_speed = MAX_PAN_TILT_SPEED;
	}
	if(cont_max_speed < MIN_PAN_TILT_SPEED)
	{
		LOGINFO("max pan tilt speed is smaller than %f", MIN_PAN_TILT_SPEED);
		cont_max_speed = MIN_PAN_TILT_SPEED;
	}
	g_free(value);
	value = NULL;
  
	LOGINFO("Max Pan Tilt Speed %f" , cont_max_speed);
  
	/* Create the axptz library */
	if (!(ax_ptz_create(&local_error))) 
	{	
		goto failure;
	}

	/* Get the application group from the PTZ control queue */
    if (!(ax_ptz_control_queue_group = ax_ptz_control_queue_get_app_group_instance(&local_error))) 
	{
		goto failure;
	}

	/* Get the supported capabilities */
	if (!(get_ptz_move_capabilities())) {
		goto failure;
	}
  
	/* Get the current status (e.g. the current pan/tilt/zoom value/position) */
	if (!(ax_ptz_movement_handler_get_ptz_status(VIDEO_CHANNEL, AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS, AX_PTZ_MOVEMENT_ZOOM_UNITLESS, &unitless_status, &local_error))) 
	{
		goto failure;
	}
	
	LOGINFO("Current Camera PTZ unitless pos - PAN:%d , TILT:%d , ZOOM:%d" , unitless_status->pan_value , unitless_status->tilt_value , unitless_status->zoom_value);
	
	AXPTZLimits *unitless_limits = NULL;
	/* Get the pan, tilt and zoom limits for the unitless space */
	if ((ax_ptz_movement_handler_get_ptz_limits(VIDEO_CHANNEL, AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS , AX_PTZ_MOVEMENT_ZOOM_UNITLESS, &unitless_limits, &local_error))) 
	{
		LOGINFO("Limits pan max: %d, pan min: %d, tilt max: %d, tilt min: %d, zoom max: %d, zoom min: %d", unitless_limits->max_pan_value, unitless_limits->min_pan_value, unitless_limits->max_tilt_value, unitless_limits->min_tilt_value, unitless_limits->max_zoom_value, unitless_limits->min_zoom_value);
		//Limits pan max: 32768, pan min: -32768, tilt max: 3641, tilt min: -16384, zoom max: 35748, zoom min: 3
		// 32768 => 180
		//-32768 => -180
		//  3641 => 20
		//-16384 => -90
		// 35748 => 24 32768 => 12 32768 ~ 35748 => 12 ~ 24
		//     3 => 1
		
	} 
	else 
	{
		goto failure;
	}
	g_free(unitless_limits);
	unitless_limits = NULL;
	
	LOGINFO("Now we got the current PTZ limits.\n");
	
	/* Get the supported capabilities */
	if (is_capability_supported("AX_PTZ_MOVE_ABS_PAN") && is_capability_supported("AX_PTZ_MOVE_ABS_TILT") && is_capability_supported("AX_PTZ_MOVE_ABS_ZOOM") && is_capability_supported("AX_PTZ_MOVE_CONT_PAN") && is_capability_supported("AX_PTZ_MOVE_CONT_TILT") && is_capability_supported("AX_PTZ_MOVE_CONT_ZOOM")) 
	{    
		/*Get the position info from presets*/
		get_path();
		LOGINFO("Preset Count - %d", preset_count);
		if(preset_count > 1)
		{
			gint i = 0;
			LOGINFO("Getting preset position info BEGIN");
			for(i = 0 ; i < preset_count ; i ++)
			{
				LOGINFO("i%d", i);
				LOGINFO("number%d" , preset_numbers[i]);
				LOGINFO("index%d" , preset_indices[i]);
				if(!ax_ptz_preset_handler_goto_preset_number(ax_ptz_control_queue_group ,VIDEO_CHANNEL , preset_indices[i] , fx_ftox(0.4f, FIXMATH_FRAC_BITS) , AX_PTZ_PRESET_MOVEMENT_UNITLESS , AX_PTZ_INVOKE_ASYNC , NULL , NULL , &local_error))
				{
					LOGINFO(local_error->message);
					goto failure;
				}
				LOGINFO("Move to preset%d position started" , preset_indices[i]);
				usleep(SLEEP_TIME_MILLISECONDS * 1000); 
	  
				if(!wait_for_camera_movement_to_finish())
				{
					goto failure;
				}
				LOGINFO("Move to preset%d position Ended - user defined order%d" , preset_indices[i], preset_numbers[i]);
				//get preset position info
				/* Get the current status (e.g. the current pan/tilt/zoom value/position) */
				unitless_status = NULL;
				if (!(ax_ptz_movement_handler_get_ptz_status(VIDEO_CHANNEL, AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS, AX_PTZ_MOVEMENT_ZOOM_UNITLESS, &unitless_status, &local_error))) 
				{
					goto failure;
				}	
				PTZ_POS* pos = g_malloc(sizeof(PTZ_POS));
				pos->pan_val = unitless_status->pan_value;
				pos->tilt_val = unitless_status->tilt_value;
				pos->zoom_val = unitless_status->zoom_value;
				tempPath = g_list_append(tempPath , pos);
				LOGINFO("PRESETNO:%d , PAN:%d , TILT:%d , ZOOM:%d" , preset_indices[i] , unitless_status->pan_value , unitless_status->tilt_value , unitless_status->zoom_value);
	
			}
			if(preset_count > 1)
			{
				for(i = preset_count - 2 ; i > 0 ; i --)
				{
					LOGINFO("number%d" , preset_numbers[i]);
					LOGINFO("index%d" , preset_indices[i]);
					if(!ax_ptz_preset_handler_goto_preset_number(ax_ptz_control_queue_group ,VIDEO_CHANNEL , preset_indices[i] , fx_ftox(0.4f, FIXMATH_FRAC_BITS) , AX_PTZ_PRESET_MOVEMENT_UNITLESS , AX_PTZ_INVOKE_ASYNC , NULL , NULL , &local_error))
					{
						LOGINFO(local_error->message);
						goto failure;
					}
					LOGINFO("Move to preset%d position Ended - user defined order%d" , preset_indices[i], preset_numbers[i]);
					usleep(SLEEP_TIME_MILLISECONDS * 1000); 
				
					if(!wait_for_camera_movement_to_finish())
					{
						goto failure;
					}
					LOGINFO("Move to preset%d position Ended" , preset_indices[i]);
					//get preset position info
					/* Get the current status (e.g. the current pan/tilt/zoom value/position) */
					unitless_status = NULL;
					if (!(ax_ptz_movement_handler_get_ptz_status(VIDEO_CHANNEL, AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS, AX_PTZ_MOVEMENT_ZOOM_UNITLESS, &unitless_status, &local_error))) 
					{
						goto failure;
					}
					PTZ_POS* pos = g_malloc(sizeof(PTZ_POS));
					pos->pan_val = unitless_status->pan_value;
					pos->tilt_val = unitless_status->tilt_value;
					pos->zoom_val = unitless_status->zoom_value;
					tempPath = g_list_append(tempPath , pos);
					LOGINFO("PRESETNO:%d , PAN:%d , TILT:%d , ZOOM:%d" , preset_count * 2 - preset_indices[i] , unitless_status->pan_value , unitless_status->tilt_value , unitless_status->zoom_value);
				}
			}
			LOGINFO("Getting preset position info END");
			LOGINFO("Completing circular path BEGIN");
			
			get_circular_path();

			LOGINFO("number of paths: %d", g_list_length(realPath));	
      
			LOGINFO("Completing circular path END");
    
			LOGINFO("Endless tour along the presets BEGIN");
      
			gboolean isRunning = TRUE;
      
			while(isRunning)
			{
				gint count = 0;
	
				LOGINFO("number of paths: %d", g_list_length(realPath));	
	
				for(it = g_list_first(realPath) ; it != NULL ; it = g_list_next(it))
				{	
	  
					/*NECESSARY PART*/
					/* Request for dropping the PTZ control */
					if (!(ax_ptz_control_queue_request(ax_ptz_control_queue_group, VIDEO_CHANNEL, AX_PTZ_CONTROL_QUEUE_DROP, &queue_pos, &time_to_pos_one, &poll_time, &local_error))) 
					{
						goto failure;
					}

					LOGINFO("Request AX_PTZ_CONTROL_QUEUE_DROP:\n");
					LOGINFO("queue_pos = %d\n", queue_pos);
					LOGINFO("time_to_pos_one = %d\n", time_to_pos_one);
					LOGINFO("poll_time = %d\n", poll_time);

					/* Get the PTZ control queue status for the application */
					if (!(ax_ptz_control_queue_request(ax_ptz_control_queue_group, VIDEO_CHANNEL, AX_PTZ_CONTROL_QUEUE_GET, &queue_pos, &time_to_pos_one, &poll_time, &local_error))) 
					{	
						goto failure;
					}

					LOGINFO("Request AX_PTZ_CONTROL_QUEUE_GET:\n");
					LOGINFO("queue_pos = %d\n", queue_pos);
					LOGINFO("time_to_pos_one = %d\n", time_to_pos_one);
					LOGINFO("poll_time = %d\n", poll_time);	
	  
					/* Get the application group from the PTZ control queue */
					if (!(ax_ptz_control_queue_group = ax_ptz_control_queue_get_app_group_instance(&local_error))) 
					{	
						goto failure;
					}
					/*NECESSARY PART*/
	  
					count ++;
					LOGINFO("Go to Path Number:%d , PAN:%d , TILT:%d , ZOOM:%d" , count , ((PTZ_POS*)(it->data))->pan_val , ((PTZ_POS*)(it->data))->tilt_val , ((PTZ_POS*)(it->data))->zoom_val);
					PTZ_POS posFrom;
					AXPTZStatus* ptz_status = NULL;
					if (!(ax_ptz_movement_handler_get_ptz_status(VIDEO_CHANNEL, AX_PTZ_MOVEMENT_PAN_TILT_UNITLESS, AX_PTZ_MOVEMENT_ZOOM_UNITLESS, &ptz_status, &local_error))) 
					{
						g_free(ptz_status);
						LOGINFO(local_error->message);
						g_error_free(local_error);
						return FALSE;
					}
	  
					posFrom.pan_val = ptz_status->pan_value;
					posFrom.tilt_val = ptz_status->tilt_value;
					posFrom.zoom_val = ptz_status->zoom_value;
					LOGINFO("Position From PAN:%d , TILT:%d , ZOOM:%d" , posFrom.pan_val , posFrom.tilt_val , posFrom.zoom_val);
	  
					LOGINFO("arrival_accuracy : %d" , arrival_accuracy);
	  
					LOGINFO("Setting speeds BEGIN");
					fixed_t pan_speed = fx_subx(((PTZ_POS*)(it->data))->pan_val , posFrom.pan_val);
					fixed_t pan_speed1 = pan_speed;
					if(pan_speed1 < 0)
						pan_speed1 = -pan_speed1;

					fixed_t tilt_speed = fx_subx(((PTZ_POS*)(it->data))->tilt_val , posFrom.tilt_val);
					fixed_t tilt_speed1 = tilt_speed;
					if(tilt_speed1 < 0)
						tilt_speed1 = -tilt_speed1;
	
					fixed_t zoom_speed = /*fx_divx(*/fx_subx(((PTZ_POS*)(it->data))->zoom_val , posFrom.zoom_val);/* , fx_itox(3 , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS);*/
					fixed_t zoom_speed1 = zoom_speed;
					if(zoom_speed < 0)
						zoom_speed1 = -zoom_speed1;
	
					if(pan_speed1 >= tilt_speed1 && pan_speed1 >= zoom_speed1)
					{
						if(pan_speed1 > 0)
						{
							pan_speed1 = fx_ftox(cont_max_speed , FIXMATH_FRAC_BITS);
							if(pan_speed < 0)
								pan_speed1 = -pan_speed1;
								
							tilt_speed1 = fx_mulx(fx_divx(fx_mulx(tilt_speed , pan_speed1 , FIXMATH_FRAC_BITS) , pan_speed , FIXMATH_FRAC_BITS) , fx_ftox(1.4f , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS);
							zoom_speed1 = fx_mulx(fx_divx(fx_mulx(zoom_speed , pan_speed1 , FIXMATH_FRAC_BITS) , pan_speed , FIXMATH_FRAC_BITS) , fx_ftox(1.4f , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS);
						}
					}
					else if(tilt_speed1 >= pan_speed1 && tilt_speed1 >= zoom_speed1)
					{
						if(tilt_speed1 > 0)
						{
							tilt_speed1 = fx_ftox(cont_max_speed , FIXMATH_FRAC_BITS);
							if(tilt_speed < 0)
								tilt_speed1 = -tilt_speed1;
							pan_speed1 = fx_divx(fx_mulx(pan_speed , tilt_speed1 , FIXMATH_FRAC_BITS) , tilt_speed , FIXMATH_FRAC_BITS);

							zoom_speed1 = fx_mulx(fx_divx(fx_mulx(zoom_speed , tilt_speed1 , FIXMATH_FRAC_BITS) , tilt_speed , FIXMATH_FRAC_BITS) , fx_ftox(1.4f , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS);
						  
							tilt_speed1 = fx_mulx(tilt_speed1 , fx_ftox(1.4f , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS);
						}
					}
					else if(zoom_speed1 >= pan_speed1 && zoom_speed1 >= tilt_speed1)
					{
						if(zoom_speed1 > 0)
						{
							zoom_speed1 = fx_ftox(cont_max_speed , FIXMATH_FRAC_BITS);
							if(zoom_speed < 0)
								zoom_speed1 = -zoom_speed1;
							tilt_speed1 = fx_mulx(fx_divx(fx_mulx(tilt_speed , zoom_speed1 , FIXMATH_FRAC_BITS) , zoom_speed , FIXMATH_FRAC_BITS) , fx_ftox(1.4f , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS);

							pan_speed1 = fx_divx(fx_mulx(pan_speed , zoom_speed1 , FIXMATH_FRAC_BITS) , zoom_speed , FIXMATH_FRAC_BITS);
							
							zoom_speed1 = fx_mulx(zoom_speed1 , fx_ftox(1.4f , FIXMATH_FRAC_BITS) , FIXMATH_FRAC_BITS);

						}
					}
	
					LOGINFO("PAN SPEED: %f , TILT_SPEED: %f , ZOOM_SPEED: %f" , fx_xtof(pan_speed1, FIXMATH_FRAC_BITS) , fx_xtof(tilt_speed1, FIXMATH_FRAC_BITS) , fx_xtof(zoom_speed1, FIXMATH_FRAC_BITS));
					LOGINFO("PAN SPEED: %d , TILT_SPEED: %d , ZOOM_SPEED: %d" , pan_speed1, tilt_speed1, zoom_speed1);
					LOGINFO("MAX SPEED: %f", cont_max_speed);
					LOGINFO("MAX SPEED %d", fx_ftox(cont_max_speed, FIXMATH_FRAC_BITS));
	
					LOGINFO("Setting speeds END");
	  
					if (!(start_continous_movement(pan_speed1, tilt_speed1,  AX_PTZ_MOVEMENT_PAN_TILT_SPEED_UNITLESS, zoom_speed1, 600.0f))) 
					{
						LOGINFO("Error occured during starting continuouse move");
						goto failure;
					}

					usleep(20 * 1000);

					LOGINFO("Move to No%d position started" , count);
					if(!wait_for_camera_arrive_to_specific_pos(((PTZ_POS*)(it->data))->pan_val , ((PTZ_POS*)(it->data))->tilt_val , ((PTZ_POS*)(it->data))->zoom_val , pan_speed1 , tilt_speed1 , zoom_speed1))
					{
						LOGINFO("Error occured during waiting");
						goto failure;
					}
					LOGINFO("Move to No%d position Ended" , count);
	  
					LOGINFO("STOPPING IN PRESET BEGIN");
					if(NPT == 0)
						usleep(preset_delay[count - 1] * 1000);//stop in preset for stop_in_preset sec
					else if(count % (NPT + 1) == 1)
						usleep(preset_delay[count / (NPT + 1)] * 1000);//stop in preset for stop_in_preset sec
					LOGINFO("STOPPING IN PRESET ENDED");
				}

			}
      
			LOGINFO("Endless tour along the presets END");
		}
		else
		{
			LOGINFO("No presets are defined.");
		}
	}
	else
	{
		LOGINFO("Absolute or Continuous movement not supported");
		goto failure;
	}
 
	/* Request for dropping the PTZ control */
	if (!(ax_ptz_control_queue_request(ax_ptz_control_queue_group, VIDEO_CHANNEL, AX_PTZ_CONTROL_QUEUE_DROP, &queue_pos, &time_to_pos_one, &poll_time, &local_error))) 
	{
		goto failure;
	}

	LOGINFO("Request AX_PTZ_CONTROL_QUEUE_DROP:\n");
	LOGINFO("queue_pos = %d\n", queue_pos);
	LOGINFO("time_to_pos_one = %d\n", time_to_pos_one);
	LOGINFO("poll_time = %d\n", poll_time);

	/* Get the PTZ control queue status for the application */
	if (!(ax_ptz_control_queue_request(ax_ptz_control_queue_group, VIDEO_CHANNEL, AX_PTZ_CONTROL_QUEUE_QUERY_STATUS, &queue_pos, &time_to_pos_one, &poll_time, &local_error))) 
	{
		goto failure;
	}

	LOGINFO("Request AX_PTZ_CONTROL_QUEUE_QUERY_STATUS:\n");
	LOGINFO("queue_pos = %d\n", queue_pos);
	LOGINFO("time_to_pos_one = %d\n", time_to_pos_one);
	LOGINFO("poll_time = %d\n", poll_time);

	/* Now we don't need the axptz library anymore, destroy it */
	if (!(ax_ptz_destroy(&local_error))) 
	{
		goto failure;
	}

	LOGINFO("%s finished successfully...\n", APP_NAME);

	/* Perform cleanup */

	if (local_error) 
	{
		g_error_free(local_error);
		local_error = NULL;
	}
	
	if (capabilities) 
	{

		for (it = g_list_first(capabilities); it != NULL; it = g_list_next(it)) 
		{
			g_free((gchar *) it->data);
		}
	}

	if(realPath)
	{
		for (it = g_list_first(realPath); it != NULL; it = g_list_next(it)) 
		{
			g_free((gchar *) it->data);
		}
	}
	if(tempPath)
	{
		for (it = g_list_first(tempPath); it != NULL; it = g_list_next(it)) 
		{
			g_free((gchar *) it->data);
		}
	}
  
	g_list_free(capabilities);
	capabilities = NULL;
	g_free(unitless_status);
	unitless_status = NULL;

	ax_parameter_free(param);
	param = NULL;

	exit(EXIT_SUCCESS);

/* We will end up here if something went wrong */
failure:

	if (local_error && local_error->message) 
	{
		LOGINFO("ERROR: %s ended with errors:\n", APP_NAME);
		LOGINFO("%s\n", local_error->message);
	}

	if (local_error) 
	{
		g_error_free(local_error);
		local_error = NULL;
	}

	/* Now we don't need the axptz library anymore, destroy it */
	ax_ptz_destroy(&local_error);

	/* Perform cleanup */

	if (local_error) 
	{
		g_error_free(local_error);
		local_error = NULL;
	}

	if (capabilities) 
	{
		for (it = g_list_first(capabilities); it != NULL; it = g_list_next(it)) 
		{
		g_free((gchar *) it->data);
		}
	}
  
	if(tempPath)
	{
		for (it = g_list_first(tempPath); it != NULL; it = g_list_next(it)) 
		{
			g_free((gchar *) it->data);
		}
	}
	if(realPath)
	{
		for (it = g_list_first(realPath); it != NULL; it = g_list_next(it)) 
		{
			g_free((gchar *) it->data);
		}
	}
	g_list_free(tempPath);
	g_list_free(realPath);
	g_list_free(capabilities);
	capabilities = NULL;
	g_free(unitless_status);
	unitless_status = NULL;


	ax_parameter_free(param);
	param = NULL;

#ifdef WRITE_TO_SYS_LOG
	closelog();
#endif

	exit(EXIT_FAILURE);
}
