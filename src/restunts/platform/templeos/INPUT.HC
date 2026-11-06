#include "NATIVE.HH"

#define TEMPLE_INPUT_QUEUE_SIZE 64
#define TEMPLE_INPUT_POLL_MS 1
#define TEMPLE_SCAN_CODE_MASK 127
#define TEMPLE_SCAN_CODE_COUNT 128
#define TEMPLE_DOS_F11_SCAN 133
#define TEMPLE_DOS_F12_SCAN 134
#define TEMPLE_DOS_CTRL_F9_SCAN 102
#define TEMPLE_DOS_SHIFT_F1_SCAN 84
#define TEMPLE_DOS_SHIFT_F11_SCAN 135
#define TEMPLE_DOS_SHIFT_F12_SCAN 136
#define TEMPLE_DOS_RIGHT_SHIFT_SCAN 54
#define TEMPLE_MOUSE_LEFT 1
#define TEMPLE_MOUSE_RIGHT 2
#define TEMPLE_MOUSE_BUTTONS 2
#define TEMPLE_INPUT_MESSAGES                                                                      \
  ((1<<MSG_KEY_DOWN)|(1<<MSG_MS_L_DOWN)|(1<<MSG_MS_L_UP)|(1<<MSG_MS_R_DOWN)|\
      (1<<MSG_MS_R_UP))

class CTempleMouseEvent
{
  legacy_s16 x,y,buttons;
};

legacy_u16 temple_key_queue[TEMPLE_INPUT_QUEUE_SIZE];
legacy_u16 temple_key_read=0,temple_key_count=0;
CTempleMouseEvent temple_mouse_queue[TEMPLE_INPUT_QUEUE_SIZE];
legacy_u16 temple_mouse_read=0,temple_mouse_count=0;
legacy_s16 temple_mouse_buttons=0;
legacy_s16 temple_mouse_left=0,temple_mouse_top=0;
legacy_s16 temple_mouse_right=TEMPLE_LOGICAL_WIDTH-1;
legacy_s16 temple_mouse_bottom=TEMPLE_LOGICAL_HEIGHT-1;

legacy_u16 TempleInputKey(legacy_s64 character,legacy_s64 scan)
{
  legacy_u16 noreg code=scan&TEMPLE_SCAN_CODE_MASK;
  if(code==SC_F9&&(scan&SCF_CTRL)) {
    if(scan&(SCF_SHIFT|SCF_ALT)) {
      return 0;
    }
    return TEMPLE_DOS_CTRL_F9_SCAN<<TEMPLE_BYTE_BITS;
  }
  if(code>=SC_F1&&code<=SC_F10) {
    if(scan&SCF_SHIFT) {
      code+=TEMPLE_DOS_SHIFT_F1_SCAN-SC_F1;
    }
    return code<<TEMPLE_BYTE_BITS;
  }
  if(code==SC_F11) {
    if(scan&SCF_SHIFT) {
      return TEMPLE_DOS_SHIFT_F11_SCAN<<TEMPLE_BYTE_BITS;
    }
    return TEMPLE_DOS_F11_SCAN<<TEMPLE_BYTE_BITS;
  }
  if(code==SC_F12) {
    if(scan&SCF_SHIFT) {
      return TEMPLE_DOS_SHIFT_F12_SCAN<<TEMPLE_BYTE_BITS;
    }
    return TEMPLE_DOS_F12_SCAN<<TEMPLE_BYTE_BITS;
  }
  if((code>=SC_HOME&&code<=SC_DELETE)&&character>=CH_SPACE) {
    return code<<TEMPLE_BYTE_BITS;
  }
  if(code==SC_CURSOR_UP||code==SC_CURSOR_DOWN||code==SC_CURSOR_LEFT||
      code==SC_CURSOR_RIGHT||code==SC_HOME||code==SC_END||code==SC_INS||
      code==SC_DELETE||code==SC_PAGE_UP||code==SC_PAGE_DOWN) {
    return code<<TEMPLE_BYTE_BITS;
  }
  if(character=='\n') {
    return '\r';
  }
  if(character==CH_SHIFT_ESC) {
    return CH_ESC;
  }
  if(character>0&&character<=TEMPLE_BYTE_MASK) {
    return character;
  }
  return 0;
}

legacy_s16 TempleMouseX(legacy_s64 x)
{
  return ClampI64(
      x*TEMPLE_LOGICAL_WIDTH/TEMPLE_SCREEN_WIDTH,temple_mouse_left,temple_mouse_right);
}

legacy_s16 TempleMouseY(legacy_s64 y)
{
  return ClampI64(
      y*TEMPLE_LOGICAL_HEIGHT/TEMPLE_SCREEN_HEIGHT,temple_mouse_top,temple_mouse_bottom);
}

legacy_void TempleInputPump()
{
  legacy_s64 argument1,argument2,message;
  legacy_u16 noreg key,noreg index;
  while(message=ScanMsg(&argument1,&argument2,TEMPLE_INPUT_MESSAGES)) {
    if(message==MSG_KEY_DOWN) {
      key=TempleInputKey(argument1,argument2);
      if(key&&temple_key_count<TEMPLE_INPUT_QUEUE_SIZE) {
        index=(temple_key_read+temple_key_count)%TEMPLE_INPUT_QUEUE_SIZE;
        temple_key_queue[index]=key;
        temple_key_count++;
      }
    } else {
      switch(message) {
        case MSG_MS_L_DOWN:
          temple_mouse_buttons|=TEMPLE_MOUSE_LEFT;
          break;
        case MSG_MS_L_UP:
          temple_mouse_buttons&=~TEMPLE_MOUSE_LEFT;
          break;
        case MSG_MS_R_DOWN:
          temple_mouse_buttons|=TEMPLE_MOUSE_RIGHT;
          break;
        case MSG_MS_R_UP:
          temple_mouse_buttons&=~TEMPLE_MOUSE_RIGHT;
          break;
      }
      if(temple_mouse_count<TEMPLE_INPUT_QUEUE_SIZE) {
        index=(temple_mouse_read+temple_mouse_count)%TEMPLE_INPUT_QUEUE_SIZE;
        temple_mouse_queue[index].buttons=temple_mouse_buttons;
        temple_mouse_queue[index].x=TempleMouseX(argument1);
        temple_mouse_queue[index].y=TempleMouseY(argument2);
        temple_mouse_count++;
      }
    }
  }
}

legacy_void RsKbInitInterrupt()
{
  temple_key_count=0;
  temple_key_read=0;
  temple_mouse_count=0;
  temple_mouse_read=0;
  temple_mouse_buttons=0;
}

legacy_void RsKbExitHandler()
{
  RsKbInitInterrupt();
}

legacy_s16 RsKbGetKeyState(legacy_s16 key)
{
  TemplePump();
  if(key<=0||key>=TEMPLE_SCAN_CODE_COUNT) {
    return 0;
  }
  if(key==TEMPLE_DOS_RIGHT_SHIFT_SCAN) {
    key=SC_SHIFT;
  }
  return Bt(kbd.down_bitmap,key);
}

legacy_s16 RsKbReadChar()
{
  legacy_u16 noreg result;
  TemplePump();
  if(!temple_key_count) {
    return 0;
  }
  result=temple_key_queue[temple_key_read];
  temple_key_read=(temple_key_read+1)%TEMPLE_INPUT_QUEUE_SIZE;
  temple_key_count--;
  if(result>=TEMPLE_WORD_SIGN_BIT) {
    return result-TEMPLE_WORD_MODULUS;
  }
  return result;
}

legacy_s16 RsDosKbGetChar()
{
  return RsKbParseKey(RsKbReadChar());
}

legacy_s16 RsKbCallReadcharCallback()
{
  return RsKbReadChar();
}

legacy_s16 RsKbChecking()
{
  TemplePump();
  if(temple_key_count) {
    legacy_u16 noreg result=temple_key_queue[temple_key_read];
    if(result>=TEMPLE_WORD_SIGN_BIT) {
      return result-TEMPLE_WORD_MODULUS;
    }
    return result;
  }
  return 0;
}

legacy_s16 RsKbCheck()
{
  TemplePump();
  temple_key_count=0;
  return 0;
}

legacy_void RsFlushStdin()
{
  while(!RsKbReadChar()) {
    Sleep(TEMPLE_INPUT_POLL_MS);
  }
}

legacy_void RsDosKbSetNumlock()
{
  // Navigation mapping is explicit; the user's lock state belongs to the OS.
}

legacy_void RsDosKbClearNumlock()
{
}

legacy_void RsDosMouseSetMinmax(
    legacy_s16 left,legacy_s16 top,legacy_s16 right,legacy_s16 bottom)
{
  temple_mouse_left=ClampI64(left,0,TEMPLE_LOGICAL_WIDTH-1);
  temple_mouse_top=ClampI64(top,0,TEMPLE_LOGICAL_HEIGHT-1);
  temple_mouse_right=ClampI64(right,temple_mouse_left,TEMPLE_LOGICAL_WIDTH-1);
  temple_mouse_bottom=ClampI64(bottom,temple_mouse_top,TEMPLE_LOGICAL_HEIGHT-1);
}

legacy_void RsDosMouseSetPosition(legacy_s16 x,legacy_s16 y)
{
  x=ClampI64(x,temple_mouse_left,temple_mouse_right);
  y=ClampI64(y,temple_mouse_top,temple_mouse_bottom);
  /* Round up to the first display pixel belonging to the logical cell.
   * Vertical cells alternate between two and three pixels after correction.
   */
  MsSet((x*TEMPLE_SCREEN_WIDTH+TEMPLE_LOGICAL_WIDTH-1)/TEMPLE_LOGICAL_WIDTH,
      (y*TEMPLE_SCREEN_HEIGHT+TEMPLE_LOGICAL_HEIGHT-1)/TEMPLE_LOGICAL_HEIGHT);
}

legacy_s16 RsDosMouseInit(legacy_s16 width,legacy_s16 height)
{
  RsDosMouseSetMinmax(0,0,width-1,height-1);
  RsDosMouseSetPosition(width/2,height/2);
  return-1;
}

legacy_void RsDosMouseGetState(legacy_s16 *buttons,legacy_s16 *x,legacy_s16 *y)
{
  CTempleMouseEvent *event;
  TemplePump();
  if(temple_mouse_count) {
    event=&temple_mouse_queue[temple_mouse_read];
    *buttons=event->buttons;
    *x=event->x;
    *y=event->y;
    temple_mouse_read=(temple_mouse_read+1)%TEMPLE_INPUT_QUEUE_SIZE;
    temple_mouse_count--;
  } else {
    *buttons=temple_mouse_buttons;
    *x=TempleMouseX(ms.pos.x);
    *y=TempleMouseY(ms.pos.y);
  }
}

legacy_u16 RsDosMouseGetButtonCount()
{
  return TEMPLE_MOUSE_BUTTONS;
}

legacy_void RsDosJoystickResetCalibration()
{
}

legacy_void RsDosJoystickSetEnabled(legacy_u8 enabled)
{
  no_warn enabled;
  // TempleOS has no joystick device service. Keyboard/mouse remain available.
}

legacy_u8 RsDosJoystickIsEnabled()
{
  return FALSE;
}

legacy_s16 RsDosJoystickGetScaledAxis(legacy_u16 axis_index)
{
  no_warn axis_index;
  return 0;
}

legacy_s16 RsDosGetJoyFlags()
{
  return 0;
}
