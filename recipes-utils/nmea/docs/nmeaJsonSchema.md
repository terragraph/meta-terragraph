
## Introduction

This document describes the Terragraph json schema for encoding standard and proprietary NMEA messages. The json encoding is used to generate NMEA thrift definitions at build time, and the NMEA thrift is used in the runtime GPS datapath.

## NMEA message name

* The first token/field in an NMEA message is its name.
* Examples: $PSTMPPS, $GNGNS

## NMEA message variants

* NMEA messages have variants.
* The same message may have different parameters depending on the source of the message ("gps" or "host") and also message parameters may have a different meaning depending on the value of other parameters in the message.
* Every message variant has a unique json descriptor. The `uniq` field of this descriptor is a unique name.

## Command responses
* Some messages from the GPS module are responses to commands from the host.
* Responses are linked to their command via the `responseto` message key.
  * If the response is unique to a specific command variant, then `responseto` is the `uniq` field of the command.
  * If a response is not unique to a specific command variant, then `responseto` is the command's NMEA message name.

## Required message keys

These keys apply to an entire message. See examples below.

```
"uniq": <string>  // Unique name for a message descriptor
                  // Examples: PSTMSETPAR_BAUDRATE, $GPZDA

"nmea": <string>  // NMEA message name. Required when "uniq" is not an NMEA
                     message name. Ex: $GPGGA, $PSTMSETPAR

"source": <string> // The source of the NMEA message.
          "host" - message from the linux host to the gps module
          "gps"  - message from the gps module

"help": <string>  // Description of the NMEA message

"optlastparam": true|false // the last parameter is optional, and may be omitted

"extracomma": true|false   // message ends with a dangling comma without a parameter
                           // ex. $FOO,1,2,*69<cr><lf>
```

## Required message parameter key: "type"

The `"type"` key describes individual message parameters. See examples below.

```
"type": <string> // The datatype of the parameter

  "int"       // decimal integer (ex. 41, -2, 1234)
  "hex"       // hex integer (ex. "0xa1b", "0xc")
              // note: hex integers must be quoted in json
  "hexstring" // string of hex digits (ex. "ABCE912045687FD61C24")
  "float"     // decimal fraction (ex. "0.7", "12.4")
  "bitmap"    // decimal integer with meaningful bits
  "string"    // string (ex. "foo")
  "lat"       // "DDMM.MMMM" Latitude (degreesMinutes.FractionalMinute)
  "lon"       // "DDDMM.MMMM" Longitude (DegreesMinutes.FractionalMinute)
```

##  Optional message parameter keys

The optional keys describe individual message parameters. See examples below.

```
"name": <string>  // Name of the parameter. Always omit for constants.
"help": <string>  // Description of the parameter. Always omit for constants.
"values": [ val1, val2, ... ] // Permitted values for a non-bitmap.
                              // Note: a single permitted value means "constant".
                              // Ex. [ 1 ] [ "foo", "bar" ]
"bits": [ bit2, bit2, ... ]   // Permitted bits in a bitmap.
                              // Ex. [ 4, 16, 128 ] [ 1 2 4 8 ]
"valueshelp": [ "val1 help", "val2 help", .... ] // Meaning of each permitted value **or** bit.
"min": <int>     // min value
"max": <int>     // max value
"len": <int>     // the exact number of digits or characters in a value
"minlen": <int>  // the min number of digits or characters in a value
"maxlen": <int>  // max number of digits or characters
```

### Example "A" - basic structure and command/response

`Message: $PSTMSTAGPSONOFF,<OnOff><cr><lf>`
```
01 {
02   "uniq": "$PSTMSTAGPSONOFF",
03   "source": "host",
04   "help": "Enable/disable the STAGPS feature",
05   "parameters": [
06     {
07       "name": "OnOff",
08       "type": "int",
09       "values": [ 0, 1 ],
10       "valuehelp": [ "Disable STAGPS", "Enable STAGPS" ]
11     }]
12 }

Notes:
02  The unique name here is the same as the NMEA message name, so the
    "nmea" field is not required.
03  Message from the linux host to the gps module.
09  The first parameter can only have two values: 0 and 1.
10  Description of the permitted values.
```
Here is one of several different responses to `$PSTMSTAGPSONOFF`

`Message: $PSTMPOLSUSPENDED*02<cr><lf>`

```
01 {
02   "uniq": "$PSTMPOLSUSPENDED",
03   "responseto": "$PSTMSTAGPSONOFF",
04   "source": "gps",
05   "help": "STAGPS engine disabled"
06 }

Notes:
06 $PSTMPOLSUSPENDED has no parameters.
```

### Example "B" - two variants of `$PSTMPPS`

```
// Set PPS Polarity -- 3.9.6 SL869 SW Authorized User Guide
$PSTMPPS,2,6,<Polarity><cr><lf>
```
```
01 {
02   "uniq": "PSTMPPS_SET_POLARITY",
03   "nmea": "$PSTMPPS",
04   "source": "host",
05   "help": "Set PPS polarity",
06   "parameters": [
07     {
08       "type": "int",
09       "values": [ 2 ]
10     },
11     {
12       "type": "int",
13       "values": [ 6 ]
14     },
15     {
16       "name": "polarity",
17       "type": "int",
18       "values": [ 0, 1 ],
19       "valueshelp": [ "not inverted", "inverted" ]
20     }]
21 }

Notes:
02  PSTMPPS_SET_POLARITY is a made up (but descriptive!) unique name.
03  PSTMPPS_SET_PPS_REFERENCE below is also a variant of "$PSTMPPS".
09  A single permitted value means "constant"
```

```
// Set PPS Reference -- 3.9.8 SL869 SW Authorized User Guide
$PSTMPPS,2,19,<Reference><cr><lf>
```
```
01 {
02   "uniq": "PSTMPPS_SET_PPS_REFERENCE",
03   "nmea": "$PSTMPPS",
04   "source": "host",
05   "help": "Set the reference time standard for generating PPS",
06   "parameters": [
07      {
08        "type": "int",
09        "values": [ 2 ] <-- constant
10      },
11      {
12         "type": "int",
13         "values": [ 19 ] <-- constant
14      },
15      {
16         "name": "reference",
17         "type": "int",
18         "values": [ 0, 1, 2, 3, 4 ],
19         "valueshelp": [ "UTC time as per USNO", "GPS time", "GLONASS time", "UTC Russia", "GLONASS GPS delta" ]
20      }]
21 }

Notes:
02  PSTMPPS_SET_PPS_REFERENCE is a made up (but descriptive!) unique name.
03  PSTMPPS_SET_POLARITY above is also a variant of "$PSTMPPS".
```
