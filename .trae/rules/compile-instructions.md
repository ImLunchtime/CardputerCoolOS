You are running commands on Kali Linux. To compile, you can run `cd ~/.espressif/v5.4.3/esp-idf/ && . ./export.sh && cd /mnt/lao8/Projects/CardputerCoolOS/` to activate the env first, then run `idf.py build`.

When added new files that should be contained in GLOB_RECURSE in CMakeLists.txt, 
remember to `idf.py fullclean` then recompile, in order to apply the changes.

Never change codes of IDF components.

You must at least try to build after every code change.