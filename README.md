# winreaper
winreaper: command to monitor and annotate a process tree

like https://github.com/Plagman/reaper but for windows

compile with:
x86_64-w64-mingw32-g++ -o winreaper.exe winreaper.cpp -D_UNICODE -DUNICODE -municode -static

run with:
winreaper.exe app.exe (args)

the process will wait for app/game to finish
