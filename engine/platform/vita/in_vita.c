/*
in_vita.c - vita input backend
Copyright (C) 2018 fgsfds

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"

#if XASH_INPUT == INPUT_VITA

#include "input.h"
#include "joyinput.h"
#include "touch.h"
#include "client.h"
#include "in_vita.h"
#include <vitasdk.h>

static int vita_deadzone_l = 30;
static int vita_deadzone_r = 30;
static SceCtrlData pad, pad_old;

qboolean vita_keyboard_on = false;
static char input_concmd[128];
static uint16_t input_text[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static uint16_t initial_text[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
static uint16_t input_title[] =
{
	'I', 'n', 'p', 'u', 't', ' ',
	'c', 'o', 'm', 'm', 'a', 'n', 'd', 0
};
static char *input_target;
static int input_target_sz;

typedef struct buttonmapping_s
{
	SceCtrlButtons btn;
	int key;
} buttonmapping_t;

static buttonmapping_t btnmap[12] =
{
	{ SCE_CTRL_SELECT, '~' },
	{ SCE_CTRL_START, K_ESCAPE },
	{ SCE_CTRL_UP, K_UPARROW },
	{ SCE_CTRL_RIGHT, K_RIGHTARROW },
	{ SCE_CTRL_DOWN, K_DOWNARROW },
	{ SCE_CTRL_LEFT, K_LEFTARROW },
	{ SCE_CTRL_LTRIGGER, K_MOUSE1 },
	{ SCE_CTRL_RTRIGGER, K_MOUSE2 },
	{ SCE_CTRL_TRIANGLE, K_SHIFT },
	{ SCE_CTRL_CIRCLE, K_BACKSPACE },
	{ SCE_CTRL_CROSS, K_ENTER },
	{ SCE_CTRL_SQUARE, K_SPACE },
};

static inline void ascii2utf( uint16_t *dst, char *src, int dstsize )
{
	if( !src || !dst ) return;
	while( *src && (dstsize-- > 0) )
		*(dst++) = (*src++);
	*dst = 0x00;
}

static inline void utf2ascii( char *dst, uint16_t *src, int dstsize )
{
	if( !src || !dst ) return;
	while( *src && (dstsize-- > 0) )
		*(dst++) = (*(src++)) & 0xFF;
	*dst = 0x00;
}

static inline void RadialDeadzone( int *x, int *y, int dead )
{
	float analogX = (float) *x;
	float analogY = (float) *y;
	float magnitude = sqrt( analogX * analogX + analogY * analogY );
	if( magnitude <= dead )
	{
		*x = 0;
		*y = 0;
	}
}

static inline void UpdateAxes( void )
{
	int left_x = pad.lx - 127;
	int left_y = pad.ly - 127;
	int right_x = pad.rx - 127;
	int right_y = pad.ry - 127;

	if( abs( left_x ) < vita_deadzone_l ) left_x = 0;
	if( abs( left_y ) < vita_deadzone_l ) left_y = 0;
	RadialDeadzone( &right_x, &right_y, vita_deadzone_r );

	Joy_AxisMotionEvent( 0, 0, left_x * 255 );
	Joy_AxisMotionEvent( 0, 1, left_y * 255 );
	Joy_AxisMotionEvent( 0, 2, right_x * -255 );
	Joy_AxisMotionEvent( 0, 3, right_y * -255 );
}

static inline void UpdateButtons( void )
{
		for( int i = 1; i < 16; i++ )
			Joy_ButtonEvent( 0, i, (pad.buttons & (1 << i)) );
}

static inline void UpdateDPad( void )
{
/*
	uint8_t hat = JOY_HAT_CENTERED;
	hat |= pad.left && JOY_HAT_LEFT;
	hat |= pad.right && JOY_HAT_RIGHT;
	hat |= pad.up && JOY_HAT_UP;
	hat |= pad.down && JOY_HAT_DOWN;
	Joy_HatMotionEvent( 0, 0, hat );
*/
	if( cls.key_dest == key_console && ( pad.buttons & SCE_CTRL_LTRIGGER ) )
	{
		Vita_IN_OpenKeyboard( input_concmd, sizeof(input_concmd) );
	}
	else
	{
		for( int i = 0; i < 12; ++i )
			if( ( pad.buttons & btnmap[i].btn ) != ( pad_old.buttons & btnmap[i].btn ) )
				Key_Event( btnmap[i].key, !!( pad.buttons & btnmap[i].btn ) );
	}
}

static inline void UpdateKeyboard( void )
{
	static SceImeDialogResult result;
	SceCommonDialogStatus status = sceImeDialogGetStatus( );
	if( status == 2 )
	{
		memset( &result, 0, sizeof(SceImeDialogResult) );
		sceImeDialogGetResult( &result );
		if( result.button == SCE_IME_DIALOG_BUTTON_ENTER )
		{
			input_target[0] = 0;
			utf2ascii( input_target, input_text, input_target_sz );
			if( input_target == input_concmd && input_target[0] )
				Cbuf_AddText( input_target ), Cbuf_AddText( "\n" );
		}
		sceImeDialogTerm( );
		vita_keyboard_on = false;
	}
}

void Vita_IN_OpenKeyboard( char *target, int target_sz )
{
	if( vita_keyboard_on ) return;
	if( !target || target_sz < 2 ) return;

	SceImeDialogParam param;
	sceImeDialogParamInit( &param );
	param.supportedLanguages = 0x0001FFFF;
	param.languagesForced = SCE_TRUE;
	param.type = SCE_IME_TYPE_BASIC_LATIN;
	param.title = input_title;
	param.maxTextLength = target_sz - 1;
	param.initialText = initial_text;
	param.inputTextBuffer = input_text;

	input_target = target;
	input_target_sz = target_sz;
	vita_keyboard_on = true;

	sceImeDialogInit(&param);
}

void Vita_IN_Init( void )
{

}

void Vita_IN_Frame( void )
{
	pad_old = pad;
	sceKernelPowerTick(0);
	sceCtrlPeekBufferPositive( 0, &pad, 1 );
	if( vita_keyboard_on )
	{
		UpdateKeyboard( );
	}
	else
	{
		UpdateDPad( );
		UpdateButtons( );
		UpdateAxes( );
	}
}

#endif // INPUT_VITA
