//
//  cmidi_structs.h
//  cmidi
//
//  Created by Joseph Mattiello on 2/16/15.
//
//

#ifndef cmidi_cmidi_structs_h
#define cmidi_cmidi_structs_h

#define USE_COREMIDI  1
#define LOGGING_LEVEL_DEBUG 0

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object

#if USE_COREMIDI
#import <CoreMIDI/CoreMIDI.h>


#if defined(DEBUG) && LOGGING_LEVEL_DEBUG
#define DLOG(fmt, ...) \
object_post((t_object *)x, fmt, ##__VA_ARGS__)
#else
#define DLOG(...)
#endif

#define ILOG(fmt, ...) \
object_post((t_object *)x, fmt, ##__VA_ARGS__)

#define ELOG(fmt, ...) \
object_error((t_object *)x, fmt, ##__VA_ARGS__)


typedef struct _coremidi_bridge {
    // Used for comm via CoreMidi
  MIDIClientRef midiClient;
  MIDIPortRef midiOutPort;
  MIDIPortRef midiInPort;

    // The endpoints of the device we're
    // communicating with (the real hardware)
  MIDIEndpointRef destination;
  MIDIEndpointRef source;
} t_coremidi_bridge;
#endif

#define SYSEX_LENGTH 1024
typedef struct _sysexState {

    // Sysex processing state
  unsigned char sysExMessage[SYSEX_LENGTH];
  unsigned int  sysExLength ;
  bool          continueSysEx;

} t_sysex_state;

typedef struct _running_status_state {
    // Support running status
  unsigned char runningStatus;
  unsigned char runningChannel;
  int runningStatusSize;
} t_running_status_state;

  ////////////////////////// object struct
struct t_cmidi;
typedef void (*t_cmidi_devices_changed_event)(struct t_cmidi *x, bool added);

typedef struct _cmidi
{
  t_object	ob;
  t_atom		val;
  t_symbol	*name;

  t_symbol	*autoconnect;

  t_cmidi_devices_changed_event deviceCallback;

    // Patch object outlets
  void		*midiOutlet;    // Incoming midi messages
  void    *inputsOutlet;  // Hookup for a umenu
  void    *outputsOutlet; // Hookup for a umenu

#if USE_COREMIDI
  t_coremidi_bridge midiBridge;
#endif

  bool connected;

    // Store state for receiving MIDI messages that
    // require storage between operations.
  t_sysex_state sysexState;
  t_running_status_state runningStatusState;

    //
} t_cmidi;

  ///////////////////////// helper structs
typedef struct _cmidi_out {
  const char *name;

#if USE_COREMIDI
  MIDIEndpointRef destination;
#endif
} t_cmidi_out;

typedef struct _cmidi_in {
  const char *name;

#if USE_COREMIDI
  MIDIEndpointRef source;
#endif
} t_cmidi_in;

typedef struct _cmidi_devices {
  t_cmidi_out *outputs;
  uint numberOfOutputs;

  t_cmidi_in *inputs;
  uint numberOfInputs;
} t_cmidi_devices;


#endif
