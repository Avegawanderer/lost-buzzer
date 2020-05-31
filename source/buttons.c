/**********************************************************
	Module for processing buttons and switches
	using bit masks
	
	Type: Source file
	Written by: AvegaWanderer 10.2013
**********************************************************/

//#include "stdint.h"
#include "buttons.h"

// Processed output
buttons_t buttons;


//---------------------------------------------//
//	Initial buttons setting
//	
//	Can be omited if action_toggle is not used and
//	there is no access to buttons before ProcessButtons() call
//---------------------------------------------//
void InitButtons(void)
{
	buttons_t *buttons_p = &buttons;
	buttons_p->raw_state = 0;
	buttons_p->action_down = 0;
	#ifdef USE_ACTION_TOGGLE
	buttons_p->action_toggle = 0;
	#endif
	#ifdef USE_ACTION_REP
	buttons_p->action_rep = 0;
	#endif
	#ifdef USE_ACTION_UP
	buttons_p->action_up = 0;
	#endif
	#ifdef USE_ACTION_UP_SHORT
	buttons_p->action_up_short = 0;
	#endif
	#ifdef USE_ACTION_UP_LONG
	buttons_p->action_up_long = 0;
	#endif
	#ifdef USE_ACTION_HOLD
	buttons_p->action_hold = 0;
	#endif
}

//---------------------------------------------//
//	Buttons processor
//	
//---------------------------------------------//
void ProcessButtons(void)
{
	static btn_type_t raw_current = 0;
	#ifdef USE_CURRENT_INVERSED
	btn_type_t raw_current_inv;
	#endif
	#ifdef USE_DELAYED
	btn_type_t raw_delayed;
	#endif
	btn_type_t raw_delayed_inv;
	#ifdef USE_BUTTON_TIMER
	static btn_type_t press_timer = 0;		
	#endif
	#ifdef USE_ACTION_UP_SHORT
	btn_type_t event_release_short = 0x00;
	#endif
	#ifdef USE_ACTION_UP_LONG
	btn_type_t event_release_long = 0x00;
	#endif
	#ifdef USE_ACTION_REP
	btn_type_t state_repeat = 0x00;
	#endif
	#ifdef USE_ACTION_HOLD
	btn_type_t event_hold = 0x00;
	#endif
	buttons_t *buttons_p = &buttons;
	
	#ifdef USE_DELAYED
	// Get the delayed versions of raw button state
	raw_delayed = raw_current;
	#endif
	raw_delayed_inv = ~raw_current;
	// Update current
	raw_current = GetRawButtonState() ^ RAW_BUTTON_INVERSE_MASK;
	#ifdef USE_CURRENT_INVERSED
	raw_current_inv = ~raw_current;
	#endif
	
	#ifdef USE_BUTTON_TIMER
	// If some button is pressed or released
	if (raw_delayed != raw_current)
	{
		if (press_timer < LONG_PRESS_DELAY)
		{
			#ifdef USE_ACTION_UP_SHORT
			event_release_short = _FF;			// Button release short
			#endif
		}
		else
		{
			#ifdef USE_ACTION_UP_LONG
			event_release_long = _FF;			// Button release long
			#endif
		}
		// Reset timer	
		press_timer = 0;
	}
	else
	{	
		// Increment timer
		if (press_timer != (LONG_PRESS_DELAY + 1))
		{
			press_timer++;
		}
		#ifdef USE_ACTION_REP
		if (press_timer >= REPEAT_DELAY)
		{
			state_repeat = _FF;
		}
		#endif
		#ifdef USE_ACTION_HOLD
		if (press_timer == LONG_PRESS_DELAY)
		{
			event_hold = _FF;
		}
		#endif
	}
	#endif	// end of USE_BUTTON_TIMER
	
	buttons_p->raw_state = raw_current;
	buttons_p->action_down = raw_current & raw_delayed_inv;
	#ifdef USE_ACTION_TOGGLE
	buttons_p->action_toggle ^= buttons_p->action_down;
	#endif
	#ifdef USE_ACTION_REP
	buttons_p->action_rep = raw_current & (raw_delayed_inv | state_repeat);
	#endif
	#ifdef USE_ACTION_UP
	buttons_p->action_up = raw_current_inv & raw_delayed;
	#endif
	#ifdef USE_ACTION_UP_SHORT
	buttons_p->action_up_short = raw_current_inv & raw_delayed & event_release_short;
	#endif
	#ifdef USE_ACTION_UP_LONG
	buttons_p->action_up_long = raw_current_inv & raw_delayed & event_release_long;
	#endif
	#ifdef USE_ACTION_HOLD
	buttons_p->action_hold = raw_current & event_hold;
	#endif
}


