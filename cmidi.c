/**
	@file
	cmidi - a Max external to help route SYSEX through Max for Live
        - Currently only implimented in CoreMIDI for OS X
	joe mattiello - mail@joemattiello.com

	@ingroup	M4L
*/

#include "cmidi_structs.h"

#if USE_COREMIDI
#include "coremidi.h"
#endif

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object

///////////////////////// function prototypes
//// standard set
void *cmidi_new(t_symbol *s, long argc, t_atom *argv);
void cmidi_free(t_cmidi *x);
void cmidi_assist(t_cmidi *x, void *b, long m, long a, char *s);

void cmidi_int(t_cmidi *x, long n);
//void cmidi_float(t_cmidi *x, double f);
void cmidi_anything(t_cmidi *x, t_symbol *s, long ac, t_atom *av);
void cmidi_list(t_cmidi *x, t_symbol *s, long argc, t_atom *argv);
void cmidi_bang(t_cmidi *x);
void cmidi_dblclick(t_cmidi *x);

void cmidi_sendTestNoteToOutput(t_cmidi *x);

  // Functions
void cmidi_autoconnect(t_cmidi *x, t_symbol *s);
void cmidi_refresh(t_cmidi *x);
void cmidi_setout(t_cmidi *x, t_symbol *s);
void cmidi_setin(t_cmidi *x, t_symbol *s);

//////////////////////// global class pointer variable
void *cmidi_class;

t_cmidi_devices *cmidi_getOutputDevices(t_cmidi *x);
t_cmidi_devices *cmidi_getInputDevices(t_cmidi *x);

t_symbol *ps_clear,*ps_append;

int C74_EXPORT main(void)
{
  t_class *c;

  c = class_new("cmidi", (method)cmidi_new, (method)cmidi_free, (long)sizeof(t_cmidi),
                0L /* leave NULL!! */, A_GIMME, 0);

    /* ----------- INPUTS ------------------------------*/
  class_addmethod(c, (method)cmidi_bang,			"bang", 0);
  class_addmethod(c, (method)cmidi_int,       "int",		A_LONG, 0);
  class_addmethod(c, (method)cmidi_list,      "list",		A_GIMME, 0);
//  class_addmethod(c, (method)cmidi_float,			"float",	A_FLOAT, 0);
  class_addmethod(c, (method)cmidi_anything,	"anything",	A_GIMME, 0);


    /* ----------- Methods aka messages ----------------*/
  class_addmethod(c, (method)cmidi_autoconnect, "autoconnect", A_DEFSYM, 0);
  class_addmethod(c, (method)cmidi_refresh,     "refresh", 0);
  class_addmethod(c, (method)cmidi_setout,      "setout", A_DEFSYM, 0);
  class_addmethod(c, (method)cmidi_setin,       "setin", A_DEFSYM, 0);

  /* ------------ Actions -------------------------------*/
  class_addmethod(c, (method)cmidi_dblclick,  "dblclick",		A_CANT, 0);

  /* ------------ Other ----------------------------------*/
  class_addmethod(c, (method)cmidi_assist,    "assist",		A_CANT, 0);

  /* ------------ Properties ------------------------------*/
  CLASS_ATTR_SYM(c, "name", 0, t_cmidi, name);
  CLASS_ATTR_SYM(c, "autoconnect", 0, t_cmidi, autoconnect);

    // Store these symbols for later usage
  ps_clear  = gensym("clear");
  ps_append = gensym("append");

  class_register(CLASS_BOX, c);
  cmidi_class = c;

  post("Loaded cmidi build %s %s", __DATE__, __TIME__);

  return 0;
}

void cmidi_assist(t_cmidi *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { //inlet
		sprintf(s, "I am inlet %ld", a);
	} 
	else {	// outlet
    switch (a) {
      case 0:
        sprintf(s, "Incoming MIDI data");
        break;
      case 1:
        sprintf(s, "MIDI Output endpoints");
        break;
      case 2:
        sprintf(s, "MIDI Input endpoints");
        break;

      default:
        sprintf(s, "I am outlet %ld", a);
        break;
    }
	}
}

void cmidi_free(t_cmidi *x)
{
  coreMidiFree(x);
}

void cmidi_dblclick(t_cmidi *x)
{
	DLOG( "I got a double-click");
  cmidi_sendTestNoteToOutput(x);
}

void cmidi_int(t_cmidi *x, long n)
{
  Byte msg = (Byte)n;
  coreMidiSendMidi(x, &msg, 1);
//  DLOG( "Int %i", n);
}

//void cmidi_float(t_cmidi *x, double f)
//{
//  DLOG( "Float %d", f);
//}

static
void cmidi_devices_changed_event(t_cmidi *x, bool added) {
  ILOG("Device %s", added ? "added" : "removed");
  if (added && x->autoconnect) {
      // Try to connect
    cmidi_setin(x, x->autoconnect);
    cmidi_setout(x, x->autoconnect);
  } else if (!added) {
      // Device removed.
      // TODO: Check to see if device removed and disconnect?
  }
}

void cmidi_autoconnect(t_cmidi *x, t_symbol *s) {
  DLOG("Called autoconnect with %s", s->s_name);

    // Store
  x->autoconnect = s;

  ILOG("Will autoconnect to device %s", s->s_name);

    // Try to connect now
  cmidi_setin(x, s);
  cmidi_setout(x, s);
}

void cmidi_setin(t_cmidi *x, t_symbol *s) {
  t_cmidi_devices *devices = cmidi_getInputDevices(x);

  DLOG("Called set in with %s", s->s_name);

  bool found = false;

  MIDIEndpointRef newInSource;

  for (int i=0; i < devices->numberOfInputs; i++) {
    t_cmidi_in in = devices->inputs[i];
    bool equal = strcmp(s->s_name, in.name) == 0;
    if (equal) {
      newInSource = in.source;
      ILOG("Set in device to %s", in.name);
      found = true;
      break;
    }
  }

  if (!found) {
    ELOG( "Could not find input named %s", s->s_name);
  } else {

      // Disconnect current source if exists
    if ( x->connected ) {
      MIDIPortDisconnectSource(x->midiBridge.midiInPort, x->midiBridge.source);
      x->connected = false;
    }

    x->midiBridge.source = newInSource;

    OSStatus status;
    status = MIDIPortConnectSource(x->midiBridge.midiInPort, x->midiBridge.source, NULL);
    if (status != noErr) {
      ELOG( "Could not connect midi endpoint to source");
    } else {
      ILOG( "Connected to midi device %s", s->s_name);
      x->connected = true;
    }
  }

  free(devices);
}

void cmidi_setout(t_cmidi *x, t_symbol *s) {

  t_cmidi_devices *devices = cmidi_getOutputDevices(x);

  DLOG( "Called set out with %s", s->s_name);

  bool found = false;

  for (int i=0; i < devices->numberOfOutputs; i++) {
    t_cmidi_out out = devices->outputs[i];
    bool equal = strcmp(s->s_name, out.name) == 0;
    if (equal) {
      x->midiBridge.destination = out.destination;
      ILOG( "Set out device to %s", out.name);
      found = true;
      break;
    }
  }

  if (!found) {
    ELOG( "Could not find output named %s", s->s_name);
  }

  free(devices);
}


void cmidi_anything(t_cmidi *x, t_symbol *s, long argc, t_atom *argv)
{
		DLOG( "Anything in %s %i", s->s_name, argc);

//	if (s == gensym("xyzzy")) {
//		DLOG( "A hollow voice says 'Plugh'");
//	} else {
//		atom_setsym(&x->val, s);
//		cmidi_bang(x);
//	}
}

void cmidi_list(t_cmidi *x, t_symbol *s, long argc, t_atom *argv)
{
  if (argc > 1) {

#if __BLOCKS__
    __block
#endif
    Byte * vals = malloc(argc);

      // Convert atoms to array of numbers
    t_max_err err;
    err = atom_getchar_array(argc, argv, argc, vals);
    if (err) {
      ELOG( "Error converting SYSEX data array");
    } else {
        // Send SYSEX data out
#if USE_COREMIDI
#if __BLOCKS__
      dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0), ^{
#endif
        coreMidiSendSysex(x, vals, argc);
        free(vals);
#if __BLOCKS__
      });
#endif
#endif
    }
  }
}

void cmidi_outputOutDevices(t_cmidi *x, t_cmidi_devices* devices) {
  t_atom a;
  void *outlet = x->outputsOutlet;

  outlet_anything(outlet,ps_clear,0,0);

  for (int i=0; i < devices->numberOfOutputs; i++) {
    t_cmidi_out outputDevice = devices->outputs[i];

      // Make a new atom with the name of the output
    atom_setsym(&a, gensym(outputDevice.name));
      // Send out a 'append name' message
    outlet_anything(outlet,ps_append,1,&a);
  }
}

void cmidi_outputInDevices(t_cmidi *x, t_cmidi_devices* devices) {
  t_atom a;
  void *outlet = x->inputsOutlet;

  outlet_anything(outlet,ps_clear,0,0);
  for (int i=0; i < devices->numberOfInputs; i++) {
    t_cmidi_in inputDevice = devices->inputs[i];

      // Make a new atom with the name of the output
    atom_setsym(&a, gensym(inputDevice.name));
      // Send out a 'append name' message
    outlet_anything(outlet,ps_append,1,&a);
  }
}

t_cmidi_devices *cmidi_getMidiDevices(t_cmidi *x) {

#if USE_COREMIDI
  t_cmidi_devices *devices = coreMidiGetMidiDevices(x);

  return devices;
#endif
}

void cmidi_sendTestNoteToOutput(t_cmidi *x) {
#if USE_COREMIDI
  coreMidiSendTestNoteToOutput(x);
#endif
}

void cmidi_bang(t_cmidi *x) {
//  cmidi_sendTestNoteToOutput(x);
}

t_cmidi_devices *cmidi_getOutputDevices(t_cmidi *x) {
#if USE_COREMIDI
  return coreMidiGetOutputDevices(x);
#endif
}
t_cmidi_devices *cmidi_getInputDevices(t_cmidi *x) {
#if USE_COREMIDI
  return coreMidiGetInputDevices(x);
#endif
}


void cmidi_refresh(t_cmidi *x)
{
  OSStatus status = MIDIRestart();
  if (status != noErr) {
    ELOG("Error refreshing midi devices");
  } else {
//    t_cmidi_devices *devices = getMidiDevices(x);
//    DLOG("%i inputs. %i outputs", devices->numberOfInputs, devices->numberOfOutputs);

    t_cmidi_devices *inDevices = cmidi_getInputDevices(x);
    t_cmidi_devices *outDevices = cmidi_getOutputDevices(x);

    DLOG("%i inputs. %i outputs", inDevices->numberOfInputs, outDevices->numberOfOutputs);

    cmidi_outputInDevices(x, inDevices);
    cmidi_outputOutDevices(x, outDevices);

//    free(devices);
    free(inDevices);
    free(outDevices);
  }
}

void cmidi_init_midi(t_cmidi *x) {
#if USE_COREMIDI
  coreMidiInit(x);
#endif
}

void *cmidi_new(t_symbol *s, long argc, t_atom *argv)
{
	t_cmidi *x = NULL;
    
	if ((x = (t_cmidi *)object_alloc(cmidi_class))) {
		x->name = gensym("");
		if (argc && argv) {
			x->name = atom_getsym(argv);
		}
		if (!x->name || x->name == gensym(""))
			x->name = symbol_unique();
		
		atom_setlong(&x->val, 0);

    x->inputsOutlet  = outlet_new(x, NULL);
    x->outputsOutlet = outlet_new(x, NULL);
    x->midiOutlet    = outlet_new(x, NULL);

      // Callback when devices get added/removed
    x->deviceCallback = cmidi_devices_changed_event;

    cmidi_init_midi(x);
  }

	return (x);
}
