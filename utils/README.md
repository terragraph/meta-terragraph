This directory contains various utlity scripts.

<hr>

This README contains all the information about the following scripts:
- "Qcom_iniToJson.py": that converts an input ini file (w/ 120 non-zero TX/RX
  Beams) for all channels to per-channel JSON Codebooks.
- "downsample_120beam_ini.py": that downsamples an input ini file (w/ 120
  non-zero TX/RX Beams) to generate an output.ini file with only 61 TX/RX beams
  which is Boardfile compatible (Boardfile can only support up to 61 TX/RX
  beams).
- "conv_qcom_cb.py": that is internally called the "Qcom_iniToJson.py" script as
  an intermediate step to generate the per-channel JSON Codebooks.

## Command Line Arguments and Usage

### Qcom_iniToJson.py
To use the "Qcom_iniToJson.py" script, this is how to use it
```
python Qcom_iniToJson.py <Full_Pathname_of_input.ini> <Full_Pathname_of_output_Files.json> -m <mode> -b <bitmap> -c <calib>
```
where,

"Full_Pathname_of_input.ini" (MANDATORY FIELD): Full Pathname of the input.ini
file  w/ 120 TX/RX beams.
- Assumptions:
    - It is assumed that the full beam range is 128  beams per tile (Beam [0 :
      127])
    - TX Beam [126] is used for TX Power Calibration and RX Beam [127] is used
      for RX Noise Calibrationzero and zero beams elsewhere.
    - Original 120 Beams are stored in ini file from Beam_orig [0 : 119].
    - Boresight [0 AZ, 0 EL] is Beam_orig [60]

"Full_Pathname_of_output_Files.json" (MANDATORY FIELD): Full Pathname of the
output.json codebooks for all channels
- Assumptions:
    - FB Firmware expects to see 4 RFIC modules data in the JSON codebook
      "regardless" of the mode of operation.
    - For Massive4 mode,
        - The input ini file will have the required 4 TX/RX RFIC modules' data
        - The output JSON codebook file will have the required 4 TX/RX RFIC
          modules' data
    - For Diversity and Massive2 modes,
        - The input ini file will have ONLY 2 TX/RX RFIC modules' data
        - The output JSON codebook file will have the required 4 TX/RX RFIC
          modules' data with 2 appended "Dummy" TX/RX RFIC module data
    - For Single Tile mode,
        - The input ini file will have ONLY 1 TX/RX RFIC module's data
        - The output JSON codebook file will have the required 4 TX/RX RFIC
          modules' data with 3 appended "Dummy" TX/RX RFIC module data
    - The "dummy" TX/RX RFIC module data, used for the appending, is just the
      first RX  RFIC module in the input ini file using the RFICbitmap. For
      example,
        - For Massive2 with input ini file which has TX/RX RFIC 0 and TX/RX RFIC
          2, the "dummy" TX/RX RFIC module will be RX RFIC 0
        - For Diversity with input ini file which has TX/RX RFIC 1 and TX/RX
          RFIC 5, the "dummy" TX/RX RFIC module will be RX RFIC 1
    - FB Firmware will discard the inactive TX/RX RFIC modules in the JSON
      codebook depends on the mode of operation

[-m string] (MANDATORY FIELD): Mode of operation
- Options: {massive2,massive4,diversity,single}

[-b #] (MANDATORY FIELD): Activated RFIC bitmap in decimal;
- Options: {1,4,16,64,5,17,65,20,68,80,10,34,130,40,136,160,85}
- Assumptions:
    - For "massive4",  possible active TX/RX RFIC modules are
        - (85)  ==> Active RFIC module 0, 2, 4, 6
    - For "massive2",  possible active TX/RX RFIC modules are
        - (5)   ==> Active RFIC module 0, 2
        - (17)  ==> Active RFIC module 0, 4
        - (65)  ==> Active RFIC module 0, 6
        - (20)  ==> Active RFIC module 2, 4
        - (68)  ==> Active RFIC module 2, 6
        - (80)  ==> Active RFIC module 4, 6
    - For "diversity", possible active TX/RX RFIC modules are
        - (10)  ==> Active RFIC module 1, 3
        - (34)  ==> Active RFIC module 1, 5
        - (130) ==> Active RFIC module 1, 7
        - (40)  ==> Active RFIC module 3, 5
        - (136) ==> Active RFIC module 3, 7
        - (160) ==> Active RFIC module 5, 7
    - For "single", possible active TX/RX RFIC modules are (1, 4, 16, 64)
        - (1)   ==> Active RFIC module 0
        - (4)   ==> Active RFIC module 2
        - (16)  ==> Active RFIC module 4
        - (64)  ==> Active RFIC module 6

[-c #] (MANDATORY FIELD): Per-channel calibration or Single-channel calibration
- Options: {1,4}
- Assumptions:
    - The input.ini file can have either
        - calibrated sector informations (beams) for a single channel (say
          channel 2) and repeated for other 3 channels; ==> Option: 1
        - or, per-channel natively calibrated ector informations (beams) for all
          of the 4 channels. ==> Option: 4
    - The structure of the input.ini file supports both cases.

### conv_qcom_cb.py
To use the "conv_qcom_cb.py" script, this is how to use it internally in the
"Qcom_iniToJson.py"
```
python conv_qcom_cb.py -f <Full_Pathname_of_input.ini> -o <Full_Pathname_of_output.ini>
```
where,

"Full_Pathname_of_input.ini"         (MANDATORY FIELD): Full Pathname of the
input.ini file  w/ 120 TX/RX beams.

"Full_Pathname_of_output_Files.json" (MANDATORY FIELD): Full Pathname of the
output.json codebooks for all channels

### downsample_120beam_ini.py
To use the "downsample_120beam_ini.py" script, this is how to use it
```
python downsample_120beam_ini.py <Full_Pathname_of_input.ini> <Full_Pathname_of_output.ini> -m <mode>
```
where,

"Full_Pathname_of_input.ini" MANDATORY FIELD): Full Pathname of the input.ini
file  w/ 120 TX/RX beams.
- Assumptions:
    - For the input.ini file:
        - Contains 128 beams
        - TX Beam[126] is used for TX Power Calibration
        - RX Beam[127] is used for RX Noise Calibration
        - Original 120 Beams are stored in ini file from Beam_orig[0 : 119]
        - Boresight [0 AZ, 0 EL] is Beam_orig[60]

"Full_Pathname_of_output.ini" (MANDATORY FIELD): Full Pathname of the
downsampled output.ini file
- Assumptions:
    - For the Beams Format for output.ini file:
        - For Massive: w/ 61 selected beams
            - Beam[0 : 60]                 = Beam_orig[0 : 2 : 118] +
              Beam_orig[118]
            - Beam[64]                     = Beam[38]
            - Beam[38] is not used (is set to ZERO beam)
        - For Divesity: w/ 30 selected beams
            - Beam[0 : 29]                 = Beam_orig[0 : 4 : 116]
            - Beam[(0 + 30) : (29 + 30)]   = Beam[30 : 59] = Beam[0 : 29]
            - Beam[60]                     = Beam[59]
            - Beam[64]                     = Beam[38]
            - Beam[38] is not used (is set to ZERO beam, QCOM limitation)
    - NOTE: Beam_orig[120] is used to pad output file to 128 beams

[-m string] (MANDATORY FIELD): Mode of operation
- Options: {massive,diversity}
- In terms of downsampling, being in either massive2, massive4, or single tile
  sould be treated the same.

## Examples of Command Line Usage

### Qcom_iniToJson.py
- diversity (Active RFIC module 1, 5):
```
python Qcom_iniToJson.py ../input/qc_div.ini ../output/ -m diversity -b 34 -c 1
```
- massive2  (Active RFIC module 0, 2):
```
python Qcom_iniToJson.py ../input/massive2.ini ../output/ -m massive2 -b 5 -c 4
```
- massive4  (Active RFIC module 0, 2, 4, 6):
```
python Qcom_iniToJson.py ../input/4assive2.ini ../output/ -m massive4 -b 85 -c 4
```
- single    (Active RFIC module 2):
```
python Qcom_iniToJson.py ../input/single.ini ../output/ -m single -b 4 -c 4
```

### downsample_120beam_ini.py
- massive2, massive4, single:
```
python downsample_120beam_ini.py ../input/massive.ini ../output/massive_test.ini -m massive
```
- diversity
```
python downsample_120beam_ini.py ../input/diversity.ini ../output/diversity_test.ini -m diversity
```
