# project3

In order to run this program, first run the makefile by typing in "make".
After the makefile has run and created the executables, type in the following command with the appropriate arguements:
./oss [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInMsToLaunchChildren] [-f logfile]

For example:
./oss -n 10 -s 3 -t 5 -i 100 -f logfile.txt

To remove all object files and executables, run:
make clean
