You are running commands on Kali Linux. To compile, you can run `cd ~/esp/esp-idf/ && . ./export.sh && cd ~/桌面/Projects/ADVCardputerOS_Cool/` to activate the env first, then run `idf.py build`.

When added new files that should be contained in GLOB_RECURSE in CMakeLists.txt, 
remember to `idf.py fullclean` then recompile, in order to apply the changes.

Never change codes of IDF components.