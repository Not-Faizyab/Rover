# RAPTOR Rover

An ESP32-CAM–based Wi-Fi rover with live MJPEG video, differential motor
control, pan/tilt servos, and a remotely controlled laser output. The rover
serves its control interface directly from the ESP32, so a separate web
server is not required for normal operation.

> **Safety:** This project can drive motors and control a laser. Lift the
> rover off the ground during first tests, keep the laser disabled while
> debugging, never point it at people, animals, vehicles, aircraft, or
> reflective surfaces, and follow the safety requirements that apply in your
> location.

## Features

- ESP32-CAM camera feed over Wi-Fi
- MJPEG stream on port `81`
- Browser control interface on port `80`
- Differential/tank drive with analog PWM output
- Independent pan and tilt servo control
- Laser output with explicit on/off commands
- Keyboard control for desktop testing (`WASD`)
- Touchscreen joysticks for mobile control
- Motor safety watchdog that stops the motors after 600 ms without a drive command
- Camera orientation correction and low-latency QVGA streaming

## Repository layout

```text
.
├── ESP32_Code/
│   └── ESP32_Code.ino   # Firmware, HTTP API, embedded control UI, and stream server
├── index.html            # Standalone browser HUD prototype
└── LICENSE               # MIT license
```

The firmware embeds its own HTML interface in `ESP32_Code.ino`. The root
`index.html` is useful as a front-end prototype or for future UI work, but it
is not served by the ESP32 firmware and currently displays simulated
readouts rather than sending commands to the rover.

## Hardware

The firmware targets an ESP32-CAM-style board using the camera pin mapping
defined in the source. You will also need:

- ESP32-CAM board with a compatible camera module
- Dual H-bridge motor driver
- Two DC motors and a suitable motor power source
- Two servos for pan and tilt
- Laser module or other low-current output device
- USB-to-serial adapter or another ESP32-CAM programming method
- Common ground between the ESP32, motor driver, servo supply, and output
  circuitry

### GPIO assignment

| Function | GPIO |
| --- | ---: |
| Left motor direction/PWM A | `13` |
| Left motor direction/PWM B | `14` |
| Right motor direction/PWM A | `15` |
| Right motor direction/PWM B | `2` |
| Pan servo | `12` |
| Tilt servo | `3` |
| Laser/output | `4` |

The camera uses the board-specific pins configured near the bottom of
`ESP32_Code.ino`. Do not change the motor, servo, or laser wiring without
checking boot-strapping pins and the exact ESP32-CAM variant. GPIO `3` is
also commonly connected to the serial receiver, so disconnect anything on
that pin if it interferes with flashing or boot.

**Power note:** Do not power motors or servos from the ESP32-CAM 3.3 V rail.
Use an appropriate external supply, connect grounds, and protect the ESP32
from voltage spikes and brownouts.

## Software requirements

- Arduino IDE 2.x or Arduino CLI
- ESP32 board support package
- An ESP32-CAM-compatible board selected in the IDE
- `ESP32Servo` library
- The ESP32 camera library included with the ESP32 Arduino core

The firmware also includes ESP32 networking and HTTP server headers supplied
by the ESP32 Arduino core. No npm install or build step is required.

## Configure and flash

1. Install the ESP32 board package in Arduino IDE.
2. Install the `ESP32Servo` library through the Library Manager.
3. Open `ESP32_Code/ESP32_Code.ino`.
4. Set the Wi-Fi credentials near the top of the file:

   ```cpp
   const char* ssid     = "YOUR_WIFI_NAME";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```

5. Select the board profile matching your ESP32-CAM module.
6. Connect the board for programming, select the correct port, and upload.
7. Open the Serial Monitor at `115200` baud.
8. After the board connects, browse to the printed address:

   ```text
   http://<ESP32_IP>/
   ```

The board must be able to join the configured Wi-Fi network. The firmware
waits for Wi-Fi during startup, so an incorrect SSID or password prevents
the HTTP servers from coming online.

## Using the rover

Open the ESP32 address in a landscape browser. The interface provides:

- **Left joystick:** drive control
- **Right joystick:** pan and tilt control
- **LASER button:** momentary laser/output control
- **W/A/S/D:** desktop drive control
- **Space:** desktop laser/output control

The camera stream is loaded from:

```text
http://<ESP32_IP>:81/stream
```

Only one stream viewer is supported by the firmware at a time. The command
server and user interface run on port `80`.

## HTTP API

The control page communicates with the firmware using HTTP GET requests.
Commands are intentionally small and stateless:

### Drive

```text
/cmd?t=drv&x=<steering>&y=<throttle>
```

`x` and `y` are normalized values from `-1.00` to `1.00`. The firmware mixes
them into left and right motor speeds:

```text
left  = y + x
right = y - x
```

Values are clamped to the `[-1, 1]` range and converted to PWM. Very small
outputs are suppressed by the motor deadzone.

### Pan/tilt

```text
/cmd?t=srv&x=<pan>&y=<tilt>
```

Both values are normalized from `-1.00` to `1.00` and mapped to servo angles.
The servos are centered at `90°` during startup.

### Laser/output

```text
/cmd?t=fire&val=1   # on
/cmd?t=fire&val=0   # off
```

The output is forced low during startup. The browser sends an explicit off
command when the button or space bar is released.

## Safety behavior and reliability

The firmware includes several safeguards for remote operation:

- The laser/output starts disabled.
- Motors are stopped during setup.
- A watchdog task stops both motors when no drive command has arrived for
  `600 ms`.
- Touch controls send commands on a fixed interval instead of every pointer
  event.
- Older in-flight drive and servo fetch requests are aborted before newer
  ones are sent.
- Camera frame buffers are returned even when a stream client disconnects.

The watchdog is not a substitute for an emergency stop, a fused power
system, or safe mechanical testing. Test with the wheels off the ground
before applying full power.

## Troubleshooting

### The board never prints an IP address

- Confirm the SSID and password in `ESP32_Code.ino`.
- Confirm the board is within Wi-Fi range.
- Check the Serial Monitor is set to `115200`.
- Confirm the selected board profile and power supply.

### The page loads but there is no video

- Open `http://<ESP32_IP>:81/stream` directly.
- Verify that another browser is not already using the single stream socket.
- Check camera seating and the camera pin mapping for your board variant.
- Reduce distance and interference between the rover and access point.

### Motors stop while driving

The watchdog stops the motors if drive commands are not received for
`600 ms`. Check Wi-Fi stability, browser focus, and motor-driver power
noise. The browser sends an explicit zero drive command when keyboard
control ends.

### Servos move in the wrong direction

Reverse the mapping in the `srv` branch of `cmd_handler`, or swap the servo
orientation mechanically. Avoid commanding angles beyond the physical
limits of the installed mechanism.

### Uploading fails

- Use the board's required boot/programming procedure.
- Disconnect peripherals attached to boot or serial pins.
- Confirm the correct serial port.
- Use a stable 5 V supply capable of handling startup current.

## Development notes

The embedded page and the root `index.html` are currently separate
implementations. When changing the control UI, update the embedded
`INDEX_HTML` string if the change is intended for the deployed rover.
Changes made only to `index.html` will not affect the page served by the
ESP32.

There is no automated test suite or build configuration in the repository.
Validate firmware changes on hardware, starting with the laser disconnected
and the drive wheels elevated.

## License

This project is licensed under the [MIT License](LICENSE).
