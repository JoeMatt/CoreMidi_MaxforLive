//
//  coremidi.c
//  cmidi
//
//  Created by Joseph Mattiello on 2/16/15.
//
//

#include "coremidi.h"
#include "cmidi_structs.h"
#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"         // required for new style Max object
#include "ext_critical.h"     // Thread safety

#import <CoreMIDI/CoreMIDI.h>
#import <CoreFoundation/CoreFoundation.h>
#import <CoreServices/CoreServices.h>
#import <mach/mach_time.h>

  // Forward delcare
const char* getName(MIDIObjectRef object);

#pragma mark - Callbacks
static void
notifyProc(const MIDINotification *message,
           void *refCon) // if MIDI setup is changed
{
  t_cmidi *x = refCon;

  bool added  = false;

  MIDINotificationMessageID messageID = message->messageID;
  switch (messageID) {
    case kMIDIMsgSetupChanged:
      break;
    case kMIDIMsgObjectAdded:
      added = true;
    case kMIDIMsgObjectRemoved:
      if (x->deviceCallback) {
        x->deviceCallback(x, added);
      }
      break;
    case kMIDIMsgPropertyChanged:
    case kMIDIMsgThruConnectionsChanged:
    case kMIDIMsgSerialPortOwnerChanged:
    case kMIDIMsgIOError:
      break;

    default:
      break;
  }

  DLOG("Midi changed %i", message->messageID);



    // Store and compare endpoints so refresh doesn't
    // cause unexpected bahavior
    //  cmidi_refresh(x);
}

  //We define a maximum length for a SysEx message.
static
void midiInputCallback (const MIDIPacketList *list,
                        void *procRef,
                        void *srcRef)
{

  t_cmidi *x = procRef;

#if 0
  DLOG(
       "Received packet list length %i \n\
       First byte: %#x First length: %i \n\
       TS %llu", list->numPackets, list->packet->data[0], list->packet->length, list->packet->timeStamp);
#endif

    //  We get the first MIDIPacket in the list.
    //  Although we use ->packet[0] to get the first packet,
    //  the other packets are not accessed by ->packet[1], [2] etc.
    //  We need to use MIDIPacketNext,  as you'll notice at the end of the function.
  const MIDIPacket *packet = list->packet;

  for (unsigned int i = 0; i < list->numPackets; i++) {
    UInt16 nBytes = packet->length;

      //      We want to check if we're gathering a SysEx message that is spread over many MIDIPackets.
      //      If it is, then we need to copy the data into the message buffer.

    Byte firstByte = packet->data[0];
    if (firstByte >= 0x80 && x->sysexState.continueSysEx) {
      ELOG("Sysex $#!*up. Data out of range ( %#x ) >= 0x80. Aborting stored SYSEX", firstByte);
        // Reset SYSEX state
      x->sysexState.sysExLength   = 0;
      x->sysexState.continueSysEx = false;
    }


      // Check if this is the end of a continued SysEx message
    if (x->sysexState.continueSysEx) {

      unsigned int lengthToCopy = MIN (nBytes, SYSEX_LENGTH - x->sysexState.sysExLength);
        // Copy the message into our SysEx message buffer,
        // making sure not to overrun the buffer
      memcpy(x->sysexState.sysExMessage + x->sysexState.sysExLength, packet->data, lengthToCopy);
      x->sysexState.sysExLength += lengthToCopy;

        //  Now we've copied the data, we check if the last byte is the SysEx End message.

        // Check if the last byte is SysEx End.
      x->sysexState.continueSysEx = (packet->data[nBytes - 1] != 0xF7);

        //  If we've finished the message, or if we've filled the buffer then we have  a complete SysEx message to process. Here we're not doing anything with it, but in a proper application we'd pass it to whatever acts on the MIDI messages.

      if (!x->sysexState.continueSysEx || x->sysexState.sysExLength == SYSEX_LENGTH) {
          // We would process the SysEx message here, as it is we're just ignoring it
          // Output sysex
        t_atom myList[x->sysexState.sysExLength];

        for (i=0; i < x->sysexState.sysExLength; i++) {
          atom_setlong(myList+i,x->sysexState.sysExMessage[i]);
        }

        ILOG("Received sysex length %i", x->sysexState.sysExLength);

          // Output list of sysex data to outlet 0
        outlet_list(x->midiOutlet,0L,x->sysexState.sysExLength, myList);

          // Debug print string values
#ifdef DEBUG
        long textSize = x->sysexState.sysExLength+1;
        char *text = malloc(textSize);
        bzero(text, sizeof(textSize));
        atom_gettext(x->sysexState.sysExLength, myList, &textSize, &text, 0);
        DLOG( "sysex data %s", text);
        free(text);
#endif

          // Reset SYSEX state
        x->sysexState.sysExLength   = 0;
        x->sysexState.continueSysEx = false;
      }
    } else {

        // If we weren't continuing a SysEx message then we need to iterate
        // over all the bytes in the MIDIPacket parsing the messages that are contained in it.
      UInt16 iByte, size;

      iByte = 0;
      while (iByte < nBytes) {
        size = 0;

          // First byte should be status
        unsigned char status = packet->data[iByte];
        if (status < 0xC0) {
          size = 3;
        } else if (status < 0xE0) {
          size = 2;
        } else if (status < 0xF0) {
          size = 3;
        } else if (status == 0xF0) {

            // MIDI SysEx then we copy the rest of the message into the SysEx message buffer
          unsigned int lengthLeftInMessage = nBytes - iByte;
          unsigned int lengthToCopy = MIN (lengthLeftInMessage, SYSEX_LENGTH);

          memcpy(x->sysexState.sysExMessage + x->sysexState.sysExLength, packet->data, lengthToCopy);
          x->sysexState.sysExLength += lengthToCopy;

          size = 0;
          iByte = nBytes;

            // Check whether the message at the end is the end of the SysEx
          x->sysexState.continueSysEx = (packet->data[nBytes - 1] != 0xF7);

          DLOG("SYEX started");

            // Reset any running status
          x->runningStatusState.runningStatus  = UCHAR_MAX;
          x->runningStatusState.runningChannel = UCHAR_MAX;
          continue;
        } else if (status < 0xF3) {
          size = 3;
        } else if (status == 0xF3) {
          size = 2;
        } else {
          size = 1;
        }

        unsigned char messageType           = status & 0xF0;
        unsigned char messageChannel        = status & 0xF;

          // Pointer to where the data value(s) of the message start
        const Byte *messageData                   = &(packet->data[iByte + 1]);

        if (messageType >= 0x80 && !x->sysexState.continueSysEx) {
            // New status type, store for next possible running status state
          x->runningStatusState.runningStatus     = messageType;
          x->runningStatusState.runningChannel    = messageChannel;
          x->runningStatusState.runningStatusSize = size;
        } else if (!x->sysexState.continueSysEx
                   && x->runningStatusState.runningStatus != UCHAR_MAX){
            // We have a message starting with a value <= 0x79
            // and we're not reading in SYSEX. We must be in a
            // running status state

            // Move the messageData pointer back. It's the first value
          messageData    = packet->data[iByte];

            // Set the correct type and channel
          messageType    = x->runningStatusState.runningStatus;
          messageChannel = x->runningStatusState.runningChannel;

            // Size is -1 because of the missing status byte
          size           = x->runningStatusState.runningStatusSize - 1;
        }

          // Now we know the size of each message, what type it is, and what channel
          // it was received on and we can pass it off to something that will parse it.
          // For this example, here is some code that just prints the message and the values.
          // Ideally this would happen on a low priority thread so that it doesn't block the
          // thread that receives the MIDI messages, but for this example it doesn't matter too much.

        switch (messageType) {
          case 0x80:
            DLOG("Note off: %d, %d", messageData[0], messageData[1]);
            break;

          case 0x90:
            DLOG("Note on: %d, %d", messageData[0], messageData[1]);
            break;

          case 0xA0:
            DLOG("Aftertouch: %d, %d", messageData[0], messageData[1]);
            break;

          case 0xB0: {
            DLOG("Control message: %d, %d", messageData[0], messageData[1]);

            Byte data[size];
            data[0] = messageType;
            data[1] = messageData[0];
            data[2] = messageData[1];

//            t_atom outAtom;
//            t_max_err err;
            for (i=0; i < size; i++) {
//              err = atom_setlong(&outAtom,data[i]);
              outlet_int(x->midiOutlet, data[i]);
            }

            break;
          }
          case 0xC0:
            DLOG("Program change: %d", messageData[0]);
            break;

          case 0xD0:
            DLOG("Change aftertouch: %d", messageData[0]);
            break;

          case 0xE0:
            DLOG("Pitch wheel: %d, %d", messageData[0], messageData[1]);
            break;

          case 0xF0: {
              // One of the realtime messages
            switch (status) {
              case 0xF1:
                  // Tick/Tock
              case 0xF2:
                  // Song Position
              case 0xF3:
                  // Song Select
              case 0xF4:
              case 0xF5:
                  // Reserved
              case 0xF6:
                  // Tune Request
              case 0xF7:
                  // End of Sysex -- Shouldn't get to here
              case 0xF8:
                  // Timing clock
              case 0xF9:
                  // Reserved
              case 0xFA:
                  // Start
              case 0xFB:
                  // Continue
              case 0xFC:
                  // Stop
              case 0xFD:
                  // Reserved
              case 0xFE:
                  // Active Sensing
              case 0xFF:
                  // System Reset
                DLOG("Realtime");
                break;

              default:
                DLOG("Unknown type: %#x", status);
                break;
            }
            break;
          }

          default:
            DLOG("Unknown type: %#x", status);
            break;
        }

        iByte += size;
      }
    }

      //      As mentioned above, to get the next MIDIPacket you need to use MIDIPacketNext.
    packet = MIDIPacketNext(packet);
  }
}

//static void
//sysexCompletionProc ( MIDISysexSendRequest *request ) {
//  t_cmidi *x = request->completionRefCon;
//  dispatch_sync(dispatch_get_main_queue(), ^{
//    DLOG( "Sysex callback %s", request->complete ? "Complete" : "Incomplete");
//  });
//}

#pragma mark - Device iteration
t_cmidi_devices *coreMidiGetInputDevices(t_cmidi *x) {

  ItemCount sourceCount = MIDIGetNumberOfSources();

  t_cmidi_devices *devices = malloc(sizeof(t_cmidi_devices));
  devices->numberOfInputs  = 0;
  devices->numberOfOutputs = 0;
  devices->inputs          = malloc(sizeof(t_cmidi_in)*sourceCount);
  devices->outputs         = NULL;

  for (ItemCount i = 0 ; i < sourceCount ; ++i) {

    MIDIEndpointRef source = MIDIGetSource(i);

    t_cmidi_in *input = malloc(sizeof(t_cmidi_in));

    input->name = getName(source);
    input->source = source;

    devices->inputs[devices->numberOfInputs] = *input;
    devices->numberOfInputs++;
  }

  DLOG( "%i inputs", devices->numberOfInputs);

  return devices;
}

t_cmidi_devices *coreMidiGetOutputDevices(t_cmidi *x) {

  ItemCount sourceCount = MIDIGetNumberOfDestinations();

  t_cmidi_devices *devices = malloc(sizeof(t_cmidi_devices));
  devices->numberOfInputs  = 0;
  devices->numberOfOutputs = 0;
  devices->inputs          = NULL;
  devices->outputs         = malloc(sizeof(t_cmidi_out)*sourceCount);

  for (ItemCount i = 0 ; i < sourceCount ; ++i) {

    MIDIEndpointRef dest = MIDIGetDestination(i);

    t_cmidi_out *output = malloc(sizeof(t_cmidi_out));

    output->name = getName(dest);
    output->destination = dest;

    devices->outputs[devices->numberOfOutputs] = *output;
    devices->numberOfOutputs++;
  }

  DLOG("%i outputs", devices->numberOfOutputs);

  return devices;
}

#pragma mark - MAX MIDI Init

void coreMidiInit(t_cmidi *x) {
  OSStatus result;

    // Create ourselves an output virtual client
  MIDIClientRef midiClient;
  result = MIDIClientCreate(CFSTR("cmidi client"), notifyProc, (void *)x, &midiClient);
  if (result != noErr) {
    ELOG("Error creating MIDI client: %s - %s",
                GetMacOSStatusErrorString(result),
                GetMacOSStatusCommentString(result));
    return;
  }

    // Add an outputport to our virtual client
  MIDIPortRef outPort;
  result = MIDIOutputPortCreate(midiClient, CFSTR("cmidi out"), &outPort);
  if (result != noErr) {
    ELOG("Error creating MIDI port: %s - %s",
                GetMacOSStatusErrorString(result),
                GetMacOSStatusCommentString(result));
    MIDIClientDispose(midiClient);
    return;
  }

  MIDIPortRef inPort;
  result = MIDIInputPortCreate(midiClient, CFSTR("cmidi in"), midiInputCallback, (void *)x, &inPort);
  if (result != noErr) {
    ELOG("Error creating MIDI port: %s - %s",
                GetMacOSStatusErrorString(result),
                GetMacOSStatusCommentString(result));
    MIDIPortDispose(outPort);
    MIDIClientDispose(midiClient);
    return;
  }

  DLOG( "CoreMIDI : Created midi endpoints");
    // Success. Assign
  x->midiBridge.midiClient  = midiClient;
  x->midiBridge.midiOutPort = outPort;
  x->midiBridge.midiInPort  = inPort;
}

void coreMidiFree(t_cmidi *x) {
  if (x->connected) {
    MIDIPortDisconnectSource(x->midiBridge.midiInPort, x->midiBridge.source);
    x->connected = false;
  }

    // Cleanup midi endpoints
  MIDIPortDispose(x->midiBridge.midiInPort);
  MIDIPortDispose(x->midiBridge.midiOutPort);
  MIDIClientDispose(x->midiBridge.midiClient);
}

bool isVirtualEndpoint(MIDIEndpointRef ref) {
  MIDIEntityRef entity = 0;
  MIDIEndpointGetEntity(ref, &entity);
  return entity == 0;
}

void coreMidiSendMidi(t_cmidi *x, Byte *data, long count) {

  bool isNull    = &x->midiBridge.destination == NULL;
  if (isNull) {
    ELOG("Endpoint not assigned. Cannot send MIDI data");
    return;
  }

  Byte buffer[2048];             // storage space for MIDI Packets
  MIDIPacketList *packetlist  = (MIDIPacketList*)buffer;
  MIDIPacket *currentpacket   = MIDIPacketListInit(packetlist);
  MIDITimeStamp timestamp     = 0;// Now
  MIDIEndpointRef destination = x->midiBridge.destination;

    // Could loop this for REALLY large sysex, won't need most likely
    //  for (int i=0; i < count; i++) {
  currentpacket = MIDIPacketListAdd(packetlist, 2048,
                                    currentpacket, timestamp, count, data);

  if (currentpacket == NULL) {
    ELOG( "MIDIPacketListAdd failed; %i byte %#x", 0, data[0]);
  }
    //  }

  OSStatus result;
  result = MIDISend(x->midiBridge.midiOutPort, destination, packetlist);
  if (result != noErr) {
    ELOG("Error sending MIDI data: %s - %s",
         GetMacOSStatusErrorString(result),
         GetMacOSStatusCommentString(result));
    return;
  } else {
    DLOG("Sent packet list of length %i", packetlist->numPackets);
  }
}

void coreMidiSendSysex(t_cmidi *x, Byte *data, long count) {

  OSStatus result;

  bool isNull    = &x->midiBridge.destination == NULL;
  if (isNull) {
    ELOG("Endpoint not assigned. Cannot send SYSEX data");
    return;
  }

  bool isVirtual = isVirtualEndpoint(x->midiBridge.destination);
  if (isVirtual) {
    ELOG("Endpoint is virtual. Cannot send SYSEX data");
    return;
  }

    // Sanity check length
  if (count < 2) {
    ELOG("Sysex data too short %i",count);
    return;
  }
    // Sanity check looks like SYSEX
  else if ( (data[0] != 0xF0) || (data[count-1] != 0xF7)) {
    ELOG("First %#x or last %#x Bytes don't look like SYSEX",data[0], data[count-1]);
    return;
  }

    //  Byte test[] = {240, 0, 32, 41, 0, 51, 0, 64, 247};

#if 0 // This totally craps out for some unknonw reasons.
      // Make SYSEX request
  MIDISysexSendRequest request;
    // Destination
  request.destination      = x->midiBridge.destination;
  request.complete         = false;
    // Data
  request.data             = test;
  request.bytesToSend      = sizeof(test);
    // Callback
  request.completionProc   = sysexCompletionProc;
  request.completionRefCon = x;

    // Send and capture result
  result =
  MIDISendSysex(&request);

    // Check for err
  if (result != noErr) {
    ELOG("Error sending SYSEX data: %s - %s",
                 GetMacOSStatusErrorString(result),
                 GetMacOSStatusCommentString(result));
    return;
  }
#else
  coreMidiSendMidi(x, data, count);
#endif
}

void coreMidiSendTestNoteToOutput(t_cmidi *x){
  MIDIEndpointRef destination = x->midiBridge.destination;

  if (!destination) {
    ELOG("No valid midi destination");
    return;
  }

  Byte buffer[1024];             // storage space for MIDI Packets
  MIDIPacketList *packetlist = (MIDIPacketList*)buffer;

  MIDITimeStamp timestamp;

  Byte msg[3]; // Simple message buffer
  MIDIPacket *currentpacket = MIDIPacketListInit(packetlist);

  msg[0] = 0x90,
  msg[1] = 0x30,
  msg[2] = 0x127;
  timestamp = mach_absolute_time();

  currentpacket = MIDIPacketListAdd(packetlist, sizeof(buffer),
                                    currentpacket, timestamp, sizeof(msg), msg);

    // Note off
  msg[0] = 0x80,
  msg[1] = 0x30,
  msg[2] = 0x00;

  timestamp += 5000;

  currentpacket = MIDIPacketListAdd(packetlist, sizeof(buffer),
                                    currentpacket, timestamp, sizeof(msg), msg);

  if (currentpacket == NULL) {
    ELOG("Null packet");
    return;
  }

  OSStatus result;

  result = MIDISend(x->midiBridge.midiOutPort, destination, packetlist);
  if (result != noErr) {
    ELOG("Error sending MIDI data: %s - %s",
                 GetMacOSStatusErrorString(result),
                 GetMacOSStatusCommentString(result));
    return;
  } else {
    DLOG("Sent packet list of length %i", packetlist->numPackets);
  }
}

t_cmidi_devices *
coreMidiGetMidiDevices(t_cmidi *x) {
    // How many MIDI devices do we have?
  ItemCount deviceCount = MIDIGetNumberOfDevices();

  t_cmidi_devices *devices = malloc(sizeof(t_cmidi_devices));

  devices->numberOfInputs  = 0;
  devices->numberOfOutputs = 0;
  devices->inputs          = malloc(sizeof(t_cmidi_in)*deviceCount);
  devices->outputs         = malloc(sizeof(t_cmidi_out)*deviceCount);

    // Iterate through all MIDI devices
  for (ItemCount i = 0 ; i < deviceCount ; ++i) {

      // Grab a reference to current device
    MIDIDeviceRef device = MIDIGetDevice(i);

      // Is this device online? (Currently connected?)
    SInt32 isOffline = 0;
    MIDIObjectGetIntegerProperty(device, kMIDIPropertyOffline, &isOffline);

    if (!isOffline) {
        // How many entities do we have?
      ItemCount entityCount = MIDIDeviceGetNumberOfEntities(device);

        // Iterate through this device's entities
      for (ItemCount j = 0 ; j < entityCount ; ++j) {

          // Grab a reference to an entity
        MIDIEntityRef entity = MIDIDeviceGetEntity(device, j);

          // Iterate through this device's source endpoints (MIDI In)
        ItemCount sourceCount = MIDIEntityGetNumberOfSources(entity);
        for (ItemCount k = 0 ; k < sourceCount ; ++k) {

            // Grab a reference to a source endpoint
          MIDIEndpointRef source = MIDIEntityGetSource(entity, k);

          t_cmidi_in inputDevice;
          inputDevice.name = getName(source);
          inputDevice.source = source;

          devices->inputs[devices->numberOfInputs] = inputDevice;
          devices->numberOfInputs++;
        }

          // Iterate through this device's destination endpoints
        ItemCount destCount = MIDIEntityGetNumberOfDestinations(entity);
        for (ItemCount k = 0 ; k < destCount ; ++k) {

            // Grab a reference to a destination endpoint
          MIDIEndpointRef dest = MIDIEntityGetDestination(entity, k);

          t_cmidi_out outputDevice;
          outputDevice.name        = getName(dest);
          outputDevice.destination = dest;

          devices->outputs[devices->numberOfOutputs] = outputDevice;
          devices->numberOfOutputs++;
        }
      }
    }
  }

  return devices;
}

#pragma mark - Internal Helpers
const char* getName(MIDIObjectRef object)
{
    // Returns the name of a given MIDIObjectRef as an NSString
  CFStringRef name = nil;
  if (noErr != MIDIObjectGetStringProperty(object, kMIDIPropertyName, &name))
    return nil;
  
  char *buffer = malloc(512);
  bool success = CFStringGetCString(name, buffer, 512, kCFStringEncodingUTF8);
  
  if (success) {
    return buffer;
  } else {
    free(buffer);
    return NULL;
  }
}