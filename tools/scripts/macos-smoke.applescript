-- Operate the packaged startup window by PID, never by its shared game title.
on checkboxValue(controls, labelText, desiredValue)
    tell application "System Events"
        repeat with uiElement in controls
            if role of uiElement is "AXCheckBox" then
                if name of uiElement is labelText then
                    if (value of uiElement as integer) is not desiredValue then click uiElement
                    if (value of uiElement as integer) is not desiredValue then
                        error "Could not set startup checkbox: " & labelText
                    end if
                    return
                end if
            end if
        end repeat
    end tell
    error "Missing startup checkbox: " & labelText
end checkboxValue

on startButton(controls)
    tell application "System Events"
        repeat with uiElement in controls
            if role of uiElement is "AXButton" then
                if name of uiElement is "Start game" then
                    if not enabled of uiElement then error "Start game is disabled"
                    return contents of uiElement
                end if
            end if
        end repeat
    end tell
    error "Missing Start game button"
end startButton

on run argv
    if (count of argv) is not 3 then error "Usage: ACTION LAUNCHER_PID GAME_DATA"
    set actionName to item 1 of argv
    set launcherPID to item 2 of argv as integer
    set gameData to item 3 of argv
    tell application "System Events"
        set launcherProcess to first application process whose unix id is launcherPID
        tell launcherProcess
            set frontmost to true
            set startupWindow to first window whose name is "Chocolate Stunts"
            set controls to entire contents of startupWindow
        end tell
    end tell
    if actionName is "configure" then
        my checkboxValue(controls, "Sound enabled", 1)
        my checkboxValue(controls, "New MIDI", 1)
        my checkboxValue(controls, "Skip intro", 0)
        my checkboxValue(controls, "Fullscreen", 0)
        set foundFolder to false
        tell application "System Events"
            repeat with uiElement in controls
                if role of uiElement is "AXTextField" then
                    if value of uiElement is gameData then set foundFolder to true
                end if
            end repeat
        end tell
        if not foundFolder then error "Startup window does not contain the fixture folder"
        my startButton(controls)
        return "Sound and New MIDI enabled; intro enabled; windowed; fixture folder verified"
    else if actionName is "start" then
        set buttonToClick to my startButton(controls)
        tell application "System Events" to click buttonToClick
        return "Activated the startup window's Start game button"
    end if
    error "Unknown startup action: " & actionName
end run
