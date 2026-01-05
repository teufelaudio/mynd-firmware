# MYND changelog

## [Unreleased]
### Fixed
- Disable standby timer when the BT module in the DFU state (OAM-1210)
- Initial charge type and charge current (OAM-1215)

## [1.4.0] - 2025-04-07
### Fixed
- Power consumption(must be below 0.5W) when the battery is fully charged (OAM-1200)

### Added
- Charger MAX voltage limit (5V) when the charger is inactive (OAM-1195)

## [1.3.0] - 2024-10-21
### Fixed
- CSB broadcaster disconnected sound icon does not play (OAM-1118)
- CSB slave connected sound icon does not play (OAM-1118)

## [1.2.1] - 2024-10-21
### Fixed
- CSB broadcaster pairing sound icon does not play (OAM-1118)

## [1.2.0] - 2024-10-18
### Fixed
- Battery low beep when power on at 1-10% battery level (OAM-719)
- USB PD role, which attempts to swap to the source role on every off to on transition (partially fixed OAM-757, OAM-1017)
- USB connection report to BT module on power on (OAM-1097)
- Playing BT pairing sound icon twice (OAM-1116)
- Missing BT connected sound icon during bootup (OAM-595)
- Auto exit CSB master when usb/aux is plugged/unplugged when BT source is still available (OAM-848)

### Removed
- DV samples support

## [1.1.0] - 2024-10-04
### Added
- The time gap between the first button press and the subsequent repeated hold actions
  (such as increasing or decreasing volume) (OAM-1083)
- Send charger status to BT module on boot/power-on (OAM-OAM-1097)

### Fixed
- Off-timer issue when disable doesn't work as expected (OAM-898)
- MCU requests BTConnected si twice on power on and creates skipping effect (OAM-1075)
- Battery level notification for the app (OAM-1066)
- MCU not exiting DFU mode (OAM-1028)
- Fix battery LED error when DUT in charging status (OAM-1070)

### Changed
- Make battery charge type persistent (OAM-1068)
- Get MCU software version request fails and causes version to be displayed/announced as 0.0.0 (OAM-928)

## [1.0.2] - 2024-09-14
### Fixed
- NTC lost threshold which was incorrect since the last NTC resistors modification

## [1.0.1] - 2024-09-13
### Fixed
- Regression of NTC sensor readings on DV samples, which can lead
  to the device shutting down immediately after booting

## [1.0.0] - 2024-09-12
### Changed
- Charge type to non-persistent property (Always BatteryFriendly charge type after power cycle)

### Fixed
- BT API: PD firmware version to audio announcement (OAM-929)
- Eco Mode status sent to BT when enabling/disabling via HW buttons (OAM-993)
- CSB_Receiver power off when broadcaster power off (OAM-979)
- BT API: max battery capacity (OAM-956)
- DFU mode source led pattern does not run when entering DFU mode (OAM-1028)
- Revert Berry and Mint color order (OAM-1032)
- Add battery history reset to factory reset (OAM-717)

## [0.27.0] - 2024-08-29
### Added
- BT API: battery capacity and battery MAX capacity (OAM-956, OAM-878)
- BT API: battery friendly charging notification (OAM-827)

### Fixed
- Current control for power bank mode, based on TI's support (OAM-687)
- Adjust dimmed brightness to make white less green (OAM-1010)

## [0.26.0] - 2024-08-15
### Fixed
- Fix CSB slave indication colour (OAM-924)
- Mid-woofer fault recovery (OAM-869)
- Fix issue with mobile app returning wrong speaker colors for BERRY and MINT

### Added
- Add PD firmware version to audio announcement (OAM-929)
- Broadcaster disconnect reason to power off receiver(OAM-979)

## [0.25.0] - 2024-08-08
### Fixed
- Charging DUT does not exit CSB mode on power off (OAM-903) (OAM-904) 
- Pressing BT+VOL+ while already in csb mode pauses playback, now has no effect
- Power on is not possible at 0% battery level (OAM-718)
- Do not power on DUT if battery voltage under 6v even if charger is plugged (OAM-909)
- Auto-Off timer value is persisted (OAM-898)
- Correctly scale LED brightness value for Actions (OAM-730)
- Disable BT button on CSB receiver (OAM-927)

## [0.24.1] - 2024-07-29
### Fixed
- Released version number

## [0.24.0] - 2024-07-26
### Fixed
- Fix water detection indication (OAM-836)(OAM-880)
- Fix incorrect LED colour of AUX source when power on and off (OAM-949)
- Channels swap (OAM-600)
- BT btn press does not cancel bt pairing (OAM-282)
- Playback does not continue after exiting if playback was active when entering CSB broadcaster or BT pairing
- No clear device list positive feedback indications (OAM-635)
- Incr/decr speed when holding VOL btns is too fast (OAM-383)

### Added
- Save and load main parameters to persistent storage
- The new DSP configuration (provided by AE)

## [0.23.0] - 2024-07-18
### Fixed
- Volume level returns to the default after reboot (OAM-893)
- Fix status led dimming (OAM-913)
- DUT repeatedly resets when a charger is connected while in an off state, causing charge voltage to oscillate btwn 0V-5V-19V (OAM-854)
- Charge indication not playing when charger is connected in off state (OAM-854)
- Fix USB sound card issue when connected to computer (OAM-658)
- Fix BT connection beep on reconnect (OAM-595)
- Repeatedly pressing BT button does not stop pairing mode (OAM-282)
- Source Status LED dims down after 1min in CSB and DFU (OAM-454)

## [0.22.0] - 2024-07-11
### Added
- MCU sends aux and usb connection notifications on init/boot-up (OAM-807)

### Fixed
- Continue source indication after interrupted by short patterns like EcoMode (OAM-873)
- Interrupted and continued infinite sound icons (OAM-837)
- Actively streaming audio does not continue streaming after entering CSB broadcastor mode (OAM-823)
- Set correct charging mode when charging is started (OAM-855)
- FW announcement feature available in Production Test Mode FW, feature is now removed from Prod Test Mode FW (OAM-137)
- POWER+BT does not route UART to USB-C and show prompt (OAM-137) (OAM-841)
- AZ command does not reset test mode via usb (OAM-137)
- AX command does not print return value (OAM-137)
- Default values set on factory reset do not print for AG, Ag commands (OAM-137)

### Changed
- Remove mute-unmute sequence switching track while audio source is usb (OAM-869)

## [0.21.0] - 2024-07-01
### Added
- Implement Eco mode on/off (OAM-777)
- Color ID support in production tests (OAM-137)
- Delay for PD controller to give it a time to finish initialization (OAM-854)
- Color ID notification for BT module(OAM-137)
- Eco Mode notification for BT module
- Moisture detection LED indication(OAM-836)
- New DSP flow with minor fine-tuning(OAM-584)

## Fixed
- Fix off timer (OAM-748)
- I2C bus control for debugging purpose (OAM-604)
- Switching audio sources as CSB broadcastor plays CSB repeats playing the CSB pairing si (OAM-714)
- Removing USB-C charger while CSB broadcasting exits CSB mode (OAM-848)

## Changed
- Trigger production tests on ShortRelease (OAM-841)

## [0.20.0] - 2024-06-20
### Added
- Battery friendly charging API call (OAM-543)
- LEds dimming after 1min idle (OAM-454)
- Power on the unit when start production test on BT+Power buttons press
- Limitation of power bank mode which depends on the battery level (Currently it's ON when battery level > 40%
and it's OFF when battery level <20%) (OAM-687)

### Changed
- Charger detection status is now checked based on charger IC status register (the charger status in PD controller obsolete) (OAM-711)
- Start/stop multichain pairing on short press buttons event
- Source & Sound Mode stauts LED color changes to orange solid on, dim down after 1min (OAM-762)

### Fixed
- CSB Master stays in CSB Master mode after it loses connection to its active audio source. Now emulates RG2 and falls back to BluetoothDisconnected (OAM-702)
- The speaker is silent when auto power off (OAM-751)
- Trigger time issue for power button (OAM-734)
- Blue LED color (OAM-779)
- Pop noise unplugging cable on aux input (OAM-716) 
- Max volume LED indication (OAM-721)
- Dropouts when unplug USB cable (OAM-789)

### Removed
- serial number from proto api

## [0.19.0] - 2024-06-12
### Added
- Long-press of POWER&BT btn combination makes a request to play the FW_ANNOUNCMENT sound icon (OAM-704)
- Missing low battery indication on power on (OAM-719)

### Fixed
- CSB Master pairing behavior not correctly implemented, now plays chain pairing si once, led flashes purple once and becomes solid (OAM-639)
- CSB slave pairing flashes purple instead of yellow (OAM-639) (OAM-678)
- CSB slave pairing mode does not exit properly when any btn besides BT is pressed (OAM-639)
- No led indication on factory reset (OAM-713)
- CSB pairing indication set to fast flashing, now set to slow flashing to sync with si's, BT, and UX spec
- Fixed bat current being too high when powered off or when charger is unplugged in Off-Mode (OAM-738)
- Fix behaviour with plugged charger and empty or malfunction battery (OAM-735)
- Pressing or holding the volume button to exit pairing mode do not change the volume or play/pause state. (OAM-602)
- The source LED is on when BT button is pressed in power-off mode (OAM-737)
- POP noise when hot-plugging USB cable or switching tracks (OAM-599)
- Charging sound icon (OAM-711)
- AV production test command not displaying the device name (OAM-137)
- Entering the AX production test command when the test mode is inactive provides no output, now returns "Test Mode Inactive" (OAM-137)
- After exiting the CSB mode, the broadcaster’s status LED is still purple (OAM-641)
- TWS/CSB_Sometimes automatically exits pairing mode (OAM-692)
- No beep when the 3rd BT device is connected successfully (OAM-618)

### Removed
- TWS Mode removed to save MCU flash space

## [0.18.0] - 2024-05-30
### Fixed
- The source LED that is off when the BT button is pressed in BT connectable mode (OAES-605)
- Tweeter Eq was not successfully being bypassed (OAM-435)

## [0.17.0] - 2024-05-24
### Added
- Automatic reboot after factory reset (OAES-667)
- Power off indication at 0% battery level (OAES-581)
- Low battery indication (10% and 5% level) (OAES-582)

### Fixed
- Set charging mode to battery friendly by default (OAES-593)
- Battery under/over temperature indication (with sound icons) (OAES-650)
- Incorrect LED indication when triggering fast charging (OAES-583)_

## [0.16.0] - 2024-05-22
### Added
- Play Positive Feedback once if max volume is reached and button is pressed again (OAES-616)
- Pressing the volume or Play/Pause button to exit pairing mode does not change the volume or play/pause state (OAES-602)
- Charge indication in Off state
- Additional battery voltage smoothing right after bootup
- Under/Over temperature protection for charging and for discharging (with the basic indication)

### Fixed
- Unit crash/hang after power on

## [0.15.1] - 2024-05-16
### Added
- NTC configuration for the new HW (DV1 samples)

## [0.15.0] - 2024-05-15
### Changed
- Flash memory layout (Note: the compatible bootloader version is >= 1.x.x)

### Added
- Play "Positive Feedback" sound icon on BT device list clean (OAES-603)
- Add "Positive Feedback" LED pattern as 1s blue LED flash
- Play "Positive Feedback" LED pattern on BT device list clean (OAES-603)
- SoundIconsActive is checked to decide if sound icon must be played
- Prod Test Mode PD version request
- Prod Test Mode HW revision
- Entering AZ command when Prod Test Mode is already activated resets Prod Test Mode
- UART via USB port routing in Prod Test Mode
- Battery charger status as notification proto message

### Fixed
- Prod Test Mode's LED Test not displaying status led pattern
- Audio Bypass Mode only bypasses the woofer but not the tweeter
- Charge voltage, in fast charge mode: 8.4V and in battery friendly mode: 8.2V (OAES-585)
- Charge control and status update based on battery voltage/current/NTC (OAES-650)

## [0.14.1] - 2024-05-08
### Fixed
- charger IC starup mode. By default it runs in low-power mode, which must be explicitly disabled 

## [0.14.0] - 2024-05-07
### Fixed
- BT source switching logic causing incorrect sound icons and source led patterns to play
- When USB-C is connected to PC, BT SP switches sources to USB even though bluetooth is connected
- AUX connection status is checked after BT is ready and connection change event is sent (OAES-638)
- BT MAC address's with colons, replaced with spaces

### Added
- Yellow source pattern for slave device identification
- HW revision detection (shown in the beginning of log messages)
- Power off battery voltage threshold (6000mV) (OAES-306)
- Factory reset led indication not playing correctly
- Critical warnings, led patterns and sound icons, for charge/discharge over/under battery temperature
- Charging led status patterns and sound icon when actively charging

## [0.13.0] - 2024-04-18
### Added
- AMPs treble level configuration
- Power+BT now powers on speaker and displays MYND$ prompt and can receive commands
- Prod Test's A8 key kest command now waits 30 seconds and reports if all keys were pressed

### Fixed
- Holding BT & PLAY combo exits TWS mode on master and/or slave
- Button functions non-responsive while in Prod Test Mode, buttons now only inactive when key test is activated
- Starting Prod Test Mode requires AZ command to be sent three times, now only requires being sent once
- Prod Test's AE battery temperature detection displays incorrect temperature
- Prod Test's A7 battery voltage detection displays incorrect voltage
- BT FW version not showing in logs

## [0.12.0] - 2024-04-11
### Added
- Key-Value persistent storage proto and actionslink APIs included
- Saving and loading of Led Brightness, Bass Level, and Treble Level persistent parameters on power off and power on, respectively
- Teufel app support for requesting persistent parameters
- Production Test Mode

### Fixed
- Factory reset not setting all applicable parameters to default based on pg. 30 of UI Spec
- Sound icons not playing due to Actions API calls from within event handlers
- BT button short-press event (requested by UI/UX) (OAES-453)

### Removed
- AMPs volume control from the MCU (before it set the volume to 0dB during startup)

## [0.11.0] - 2024-03-25
### Added
- Source LED will now fade out 1 minute after BT|AUX|USB connected signal is given
- MCU now reports battery level to BT module

### Fixed
- Speaker powers off when actively streaming for greater than the specified idle time due to logic that does not consider a change of streaming state from active to inactive as user activity

## [0.10.0] - 2024-03-19
### Fixed
- Amplifier startup sequence not being executed as specified in the datasheet (both amps)
- Source LED not staying purple when the speaker is in TWS/CSB mode and the source is AUX or USB
- Speaker turning itself on when plugged to a USB power supply on EV3/FOT HW
- IO expander not being reinitialized properly after a power cycle with USB power supply connected

## [0.9.0] - 2024-03-13
### Changed
- Removed charger configuration (now expected to be configured by USB PD controller)

### Fixed
- Double press events not working on power button (prevents toggling fast charging)

## [0.8.1] - 2024-03-06
### Changed
- Removed automatically powering off speaker if battery voltage is below 6 V

## [0.8.0] - 2024-03-05
### Added
- Battery level indications now reflect the battery level (battery level calculation is not accurate yet, just a temporary linear approximation)
- Automatically power off speaker if battery voltage is below 6 V
- Play positive feedback sound icon when the volume up button is pressed and the BT volume is already at max volume
- Support for HW with extra USB multiplexer for USB PD controller IC
- Reconnection command is sent to BT module on BT wake up event (short press on BT button)

### Fixed
- Timing between power off sound icon playing and amps being muted/disabled to prevent music being heard after power off sound icon
- TWS pairing is now properly stopped using the exit_tws_mode command when pressing any button while in TWS pairing mode
- Command to stop pairing no longer sends set_pairing_mode(IDLE) when BT module is in TWS/CSB mode

## [0.7.0] - 2024-02-27
### Added
- Toggle fast charging when Power button is double-pressed (toggle between 2.5A and 5A charge current)
- LED indication for fast charging mode

### Fixed
- I2C writes to charger IC with incorrect data due to mistake in TPS25751 reference manual
- Eco mode configuration

## [0.6.0] - 2024-02-22
### Added
- MCU FW update support via USB
- Power+BT+Minus button combination pressed for 8 seconds puts BT module in DFU mode
- Bluetooth and multichain now stop pairing if either the power, play, plus, or minus button is pushed
- Synchronize amp shutdown with BT module power off sequence to allow playing the power off sound icon
- Play power on sound icon when the speaker is powered on
- Play power off sound icon when the speaker is powered off
- Play BT connected sound icon when the speaker is connected to a BT device
- Play BT disconnected sound icon when the speaker is disconnected from a BT device
- Play BT pairing sound icon when the speaker is in BT pairing mode
- Play BT TWS pairing sound icon when the speaker is in TWS pairing mode
- Play BT TWS connected sound icon when the speaker is connected to a TWS device
- Play BT TWS disconnected sound icon when the speaker is disconnected from a TWS device
- Play BT CSB pairing sound icon when the speaker is in CSB pairing mode
- Play BT CSB connected sound icon when the speaker is connected to a CSB device
- Play BT CSB disconnected sound icon when the speaker is disconnected from a CSB device
- Power+Vol+ VVLP factory reset now clears the BT device list and plays the positive feedback sound icon
- LED fade out effect when the speaker is powered off
- Status LED shows battery level for 6 seconds when the speaker is powered on
- Status LED shows battery level for 6 seconds when the power button is pressed
- Charge current in the charger set to 2.5A in the MCU (temporary while we wait for USB PD FW)
- Charge voltage in the charger set to 8.4V in the MCU (temporary while we wait for USB PD FW)
- Auto power off after 10 minutes of inactivity
- Support for Eco mode by powering on unit with BT button pressed
  - NOTE: The Eco mode configuration provided by acoustics is loaded, but it seems to not work properly yet, needs investigation

### Changed
- Power+Minus button combination pressed for 8 seconds now reboots the speaker into bootloader mode
- Bypass mode LED color changed from yellow to white to avoid confusion with battery level
- USB PD controller driver changed to support TPS25751 instead of TPS25750

## [0.5.0] - 2024-01-30
### Added
- Speaker turns off when the BT module sends a TWS/CSB master power off notification
- Sending Play/Pause USB HID command to BT module when the play/pause button is pressed and source is USB
- Sending Next Track USB HID command to BT module when the next button is pressed and source is USB
- Sending Previous Track USB HID command to BT module when the previous button is pressed and source is USB
- Short press on BT button when source is BT switches to USB source if the USB source is available

### Changed
- Volume control now uses the BT module's volume control instead of the amps' DSP gain control
- Volume no longer artificially limited by arbitrary MCU gain limits (gain is only set by amps configuration on startup)
- Short press on BT button no longer switches between A2DP1 and A2DP2 if two devices are connected
- Removed logging ISNS ADC values
- Adjusted LED indications according to UX spec 1.0
- Woofer and tweeter amp configurations updated with preliminary DSP tuning

## [0.4.0] - 2024-01-23
### Added
- Source LED pulses blue slowly when source is BT and no devices are connected
- Source LED turns off when BT module is connected as TWS slave or in CSB receiver mode
- BT+Minus button combination pressed for 1.5 seconds sends exit CSB mode command to BT module if CSB state is not disabled
- BT+Minus button combination pressed for 1.5 seconds sends exit TWS mode command to BT module if TWS connection state is not disconnected

### Changed
- ADC values now are printed as a difference of averages instead of raw values
- Increased ADC sampling rate to 100 samples per second
- Improved latency of I2C commands sent to the charger IC through the USB PD chip

### Fixed
- Aux connection events before the BT module is ready are no longer missed
- Charger IC I2C address is now correctly set to 0x6B (previously shifted to the left by 1 bit)
- Invalid commands being sent to USB PD controller to perform I2C reads/writes on the charger IC
- I2C error handler not resetting the I2C peripheral correctly

## [0.3.0] - 2024-01-17
### Added
- Source LED pulses blue when BT is in pairing mode
- Source LED pulses purple when BT is in TWS/Chain pairing mode

### Changed
- On power off the MCU sends a command to the BT module to change power state to OFF instead of just cutting power
- When the BT module reports that power state changed to OFF, the MCU cuts power to the BT module

### Fixed
- Triple presses triggering single + double + triple press actions
- Single press on BT button not switching to BT source if source is AUX/USB
- Single press on BT button not switching between A2DP1 and A2DP2 sources if two devices are connected
- BT wake up event being sent on single release instead of single press
- DMA transferring wrong data from ADC peripheral, printing wrong values to debug UART

## [0.2.0] - 2024-01-09
### Added
- MCU sends USB connection change notifications to BT module when USB cable is connected/disconnected
- Printing ADC values of ISENS and ISNS_REF pins to debug UART for development purposes
- Envelop tracking is manually disabled after applying bypass mode configuration on woofer amp
- EQ is manually disabled after applying bypass mode configuration on woofer amp
- EQ is manually disabled after applying bypass mode configuration on tweeter amp
- DRC is manually disabled after applying bypass mode configuration on tweeter amp

### Changed
- TWS pairing command now uses the TWS AUTO option instead of master/slave
- CBS pairing command now uses the CBS AUTO option instead of broadcaster/receiver
- MCU no longer sends pairing/reconnection commands to BT module when the BT module finishes booting up
- Volume no longer artificially limited to -4 dB for woofer and -8 dB for tweeter in bypass mode
- Maximum volume increased from 0 dB to +20 dB
- Woofer amp configuration
- Removed switching audio source to AUX when BT button is pressed for 1.5 to 4 seconds

### Fixed
- Multi-button press combinations not working correctly when both buttons are pressed at exactly the same time

## [0.1.5] - 2023-12-13
### Added
- BT+Play button combination pressed for 1.5 seconds starts TWS pairing as master if a device is already connected
- BT+Play button combination pressed for 1.5 seconds starts TWS pairing as slave if no devices are connected
- BT press of 1.5 to 4 seconds changes the source to aux when the button is released
  - This is just for testing purposes, since aux detection doesn't seem to work yet on EV2
  - The button must be released before 4 seconds, otherwise the source switch is not triggered

### Fixed
- Loading amp configuration writing one extra byte on each burst write
- Woofer amp configuration not being loaded correctly due to boost converter not being enabled before writing to amp registers

## [0.1.4] - 2023-12-07
### Added
- BT+Plus button combination pressed for 1.5 seconds starts multichain as broadcaster if a device is already connected
- BT+Plus button combination pressed for 1.5 seconds starts multichain as receiver if no devices are connected
- BT+Minus button combination pressed for 1.5 seconds stops pairing/broadcasting/receiving
- Power+Minus button combination pressed for 8 seconds puts BT module in DFU mode
- BT LED turns red when BT module is in DFU mode

### Changed
- Multi-button press implementation (different button combinations are considered different inputs)
- BT clear device list now requires holding the BT button for 8 seconds instead of 4
- MCU notifies BT module about changes in aux connection instead of requesting source changes

## [0.1.3] - 2023-12-04
### Changed
- Speaker turns on by default when power is pressed long enough for the system to initialize
- Reenabled UI in bypass mode
- Limit max tweeter volume to -8 dB in bypass mode
- Limit max woofer volume to -4 dB in bypass mode
- Volume step size changed from 6 dB per press to 1 dB per press
- Improved synchronization between BT module I2S and amp configuration

### Fixed
- Button releases happening before debounce period not registering correctly

## [0.1.2] - 2023-12-04
### Added
- Support for bypass mode by powering on unit with play/pause button pressed
- Support for amp debugging mode by triple-pressing BT button

### Changed
- Ported project from Zephyr to FreeRTOS
- Status LED is now green when unit is powered on

### Fixed
- Amp configurations being affected by changes to volume registers
- Wrong amp configurations due to error in page/book register handling

## [0.1.1] - 2023-10-20
### Added
- Bluetooth module initialization
- USB PD controller initialization
- Charger initialization
- Tweeter amp initialization and configuration
- Subwoofer initialization and configuration
- Basic UI handling
- Power controls (press power button to power on, long press to power off)
- Start BT pairing (long press BT button)
- Volume controls using amps - no synchronization with BT module yet (plus/minus buttons)
- Play/pause (press play/pause button)
- Next track (double press play/pause button)
- Previous track (triple press play/pause button)
- Audio jack detection
- Send command to BT module to switch to analog source on audio jack connection
- Send command to BT module to switch to A2DP1 source on audio jack disconnection

## [0.1.0] - 2023-02-07
### Added
- Power button input handling
- Plus button input handling
- Minus button input handling
- Initialization of Bluetooth module
- Initialization of amplifiers
- Initialization of USB PD controller
- Initialization of boost converter
- Initialization of USB switch
- Initialization of IO expander for button/LEDs
- Basic audio thread implementation
- Basic Bluetooth thread implementation
- Basic main thread implementation
- Basic UI thread implementation
- Basic speaker data model
- Abstraction layer for Bluetooth UART
- Abstraction layer for shared I2C
- Abstraction layer for USB PD I2C
- Abstraction layer for amps
- Abstraction layer for Bluetooth module
- Abstraction layer for battery charger
- Abstraction layer for boost converter
- Abstraction layer for IO expander
- Abstraction layer for power supply
- Abstraction layer for USB PD controller
- Abstraction layer for USB switch
- Driver for tweeter amplifier TAS5805M
- Driver for subwoofer amplifier TAS5225P
- Driver for charger IC BQ25723
- Driver for software button input handling
- Driver for IO expander AW9523B
- Driver for USB PD controller TPS25750
