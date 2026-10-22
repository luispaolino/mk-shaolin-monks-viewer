# Controllers

The port uses SDL3 Gamepad for Xbox, PlayStation, and other mapped controllers.
Pair your Bluetooth controller in Windows before use. USB uses the same input mapping.
The port detects connections and disconnections while it runs.
Generic controller support depends on the Windows driver and the SDL mapping.
The game continues to display its original Xbox button prompts.

## Player assignment

In single-player mode, the keyboard and first controller both control player 1.
You can alternate between them during play.

When you enter Ko-op with a keyboard and one controller, the keyboard controls player 1.
The controller controls player 2.
With two controllers connected when you enter Ko-op, the controllers control players 1 and 2.
The keyboard remains inactive for that assignment.
Assignments remain stable during disconnection so the other player keeps the same character.

The keyboard alone supplies one input source.
Connect a controller to provide the second source for local Ko-op.
The port advertises available inputs to the original menu before character selection.
An available second source does not copy player 1 commands to player 2.

## Default keyboard mapping

| Key | Xbox input |
| --- | --- |
| W, A, S, D | Left stick |
| Arrow keys | Direction pad |
| Enter | Start |
| Backspace | Back |
| Space | A |
| Escape or Ctrl | B |
| J | X |
| K | Y |
| C | Black |
| Q | White |
| Shift | Left trigger |
| E | Right trigger |
| F9 | FPS and frame time display |

## Mouse

Mouse movement controls the original right-stick camera input.
Scripted scenes and fixed camera areas can restrict movement.
Use the Mouse tab in the launcher to set sensitivity, vertical inversion, and button assignments.

| Mouse input | Default Xbox input |
| --- | --- |
| Movement | Right stick |
| Left button | X |
| Right button | B |
| Middle button | Y |
| Side button 1 | Black |
| Side button 2 | White |

Click inside the game or use the keyboard to capture the pointer.
The first click used to capture the pointer does not activate its game action.
F10 releases or captures the pointer. After releasing it, you can click the title bar and window buttons.
Alt+Tab, minimizing, and closing the game release the pointer automatically.
Capture works in windowed, borderless, and fullscreen modes.
The captured pointer stays inside the game client area on the selected monitor.
Mouse and keyboard follow the same automatic player assignment.
Use the keyboard or controller for the original menus.

## PlayStation mapping

| PlayStation input | Xbox input |
| --- | --- |
| Cross | A |
| Circle | B |
| Square | X |
| Triangle | Y |
| Options | Start |
| Share | Back |
| L1 / R1 | White / Black |
| L2 / R2 | Left / right trigger |

Button positions follow the Xbox layout. Combat actions follow the configuration selected inside the original game.
Use a directly recognized controller when possible.
An external input translator can expose physical and virtual devices at the same time.
That can appear as two controllers.

## Configuration

Use the launcher to change dead zone, vibration, and button mappings.
Player assignment follows the current game mode automatically.

You can place a custom `gamecontrollerdb.txt` in the application data directory.
The file uses the SDL mapping format. The port does not download mappings during play.
