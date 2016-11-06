//
//  coremidi.h
//  cmidi
//
//  Created by Joseph Mattiello on 2/16/15.
//
//

#ifndef __cmidi__coremidi__
#define __cmidi__coremidi__

#include <stdio.h>
#include "cmidi_structs.h"

  // TODO Define an interface that's implimentation agnostic
void coreMidiInit(t_cmidi *x);
void coreMidiFree(t_cmidi *x);
void coreMidiSendTestNoteToOutput(t_cmidi *x);
t_cmidi_devices * coreMidiGetMidiDevices(t_cmidi *x);

t_cmidi_devices *coreMidiGetOutputDevices(t_cmidi *x);
t_cmidi_devices *coreMidiGetInputDevices(t_cmidi *x);

void coreMidiSendSysex(t_cmidi *x, Byte *data, long count);
void coreMidiSendMidi(t_cmidi *x, Byte *data, long count);

#endif /* defined(__cmidi__coremidi__) */
