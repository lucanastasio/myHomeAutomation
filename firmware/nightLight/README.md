# Night Light Firmware

## Operation

- **Emergency mode**: if enabled takes priority, the LED turns on when main power goes missing and remains in this state until it's restored.
- **Night mode**: if enabled, the LED turns on (with a fading effect) if the light level is below the threshold and a movement is detected, and remains in this state until the timeout expires, if another movement is detected before the timeout expires the timeout is restarted.

## Settings

### Menu navigation

- User settings can be entered through a single push button and the feedback is given by LED blinks.
- The settings menu is entered with a button long press:
    - The LED blinks as many times (at a constant rate) as the index of the setting currently selected then makes a long pause.
    - The setting index is changed with a short press and wraps around when reaching the last index.
    - The currently selected setting can be edited by entering the edit mode with a long press:
        - When editing a setting, the LED blinks as many times (at a constant rate but slightly faster than before) as the currently selected value for the setting then makes a long pause.
        - The value is changed with a short press and wraps around when reaching the maximum value.
        - With a long press the menu goes back to settings navigation to allow changing other settings.
    - The menu exits when a timeout is reached (when the button is not pressed for some time).
    - **NOTE:** the setting value is applied and saved even if not returning to settings navigation before the timeout.


### Settings table

|Index  |Description                |Type       |Value mapping                      |
|-------|---------------------------|-----------|-----------------------------------|
|1      |Emergency mode enable      |Boolean    |1=OFF / 2=ON                       |
|2      |Emergency mode brigthness  |1 to 5     |1=12.5% 2=25% 3=50% 4=75% 8=100%   |
|3      |Night ligth mode enable    |Boolean    |1=OFF / 2=ON                       |
|4      |Night mode brightness      |1 to 5     |1=12.5% 2=25% 3=50% 4=75% 8=100%   |
|5      |Night mode threshold       |1 to 3     |1=0.01lx 2=0.1lx 3=1lx             |
|6      |Night mode timeout         |1 to 4     |1=15s 2=30s 3=45s 4=60s            |


