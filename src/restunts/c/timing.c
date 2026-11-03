#include "externs.h"
#include "fatal.h"
#include "platform.h"
#include "timing.h"

static legacy_u32 timer_wait_target;
static legacy_s8 input_callback_overflow_message[] =
	"NO ROOM LEFT ON TIMER INTERRUPT ROUTINE LIST\r";

void timer_reg_callback(void(far *callback)(void))
{
	if (dos_timer_register_callback(callback) == DOS_TIMER_CALLBACK_REGISTRATION_FAILED) {
		fatal_error(input_callback_overflow_message);
	}
}

void timer_remove_callback(void(far *callback)(void))
{
	dos_timer_unregister_callback(callback);
}

legacy_u32 timer_get_delta_alt(void)
{
	return timer_get_delta();
}

legacy_u32 timer_set_deadline(legacy_u32 ticks)
{
	timer_wait_target = timer_get_counter() + ticks;
	return timer_wait_target;
}

legacy_u32 timer_wait_for_deadline(void)
{
	legacy_u32 res;
	do {
		res = timer_get_counter();
	} while (res < timer_wait_target);

	return res;
}

legacy_s16 timer_deadline_reached(void)
{
	return timer_get_counter() >= timer_wait_target;
}

legacy_u32 timer_wait_ticks(legacy_u32 ticks)
{
	legacy_u32 target = timer_get_counter() + ticks;

	legacy_u32 res;
	do {
		res = timer_get_counter();
	} while (res < target);

	return res;
}

static legacy_u32 secondary_timer_target(void)
{
	return ((legacy_u32)slow_timer_deadline_high << LEGACY_WORD_BITS) | slow_timer_deadline_low;
}

static legacy_s16 secondary_timer_target_reached(legacy_u32 current, legacy_u32 target)
{
	return (legacy_u16)(current >> LEGACY_WORD_BITS) >= (legacy_u16)(target >> LEGACY_WORD_BITS) &&
		   (legacy_u16)current >= (legacy_u16)target;
}

legacy_u32 slow_timer_set_deadline(legacy_u32 ticks)
{
	legacy_u32 target = (legacy_u32)(timer_get_slow_counter() + ticks);
	slow_timer_deadline_low = (legacy_u16)target;
	slow_timer_deadline_high = (legacy_u16)(target >> LEGACY_WORD_BITS);
	return target;
}

legacy_s16 slow_timer_deadline_reached(void)
{
	return secondary_timer_target_reached(timer_get_slow_counter(), secondary_timer_target());
}

legacy_u32 slow_timer_wait_ticks(legacy_u32 ticks)
{
	legacy_u32 target = (legacy_u32)(timer_get_slow_counter() + ticks);
	legacy_u32 current;
	do {
		current = timer_get_slow_counter();
	} while (!secondary_timer_target_reached(current, target));
	return current;
}
