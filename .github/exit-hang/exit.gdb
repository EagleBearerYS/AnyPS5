set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
handle all nostop noprint pass
break RtlExitUserProcess
run
python
import gdb
text = None
for line in gdb.execute("info sharedlibrary", to_string=True).splitlines():
    if "libwinpthread-1.dll" in line:
        text = int(line.split()[0], 16)
if text is None:
    print("COND_LOCKED unknown")
else:
    lock = text - 0x1000 + 0xd030
    print("WINPTHREAD_TEXT", hex(text))
    print("COND_LOCKED", hex(lock), gdb.execute("x/gx %d" % lock, to_string=True).strip())
end
info sharedlibrary
info threads
thread apply all bt 40
continue
