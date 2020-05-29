

.PHONY: clean

vr: video_reader.cpp
	g++ video_reader.cpp -o vr -Wall -ggdb -I/usr/include -L/usr/lib -lavutil -lavformat -lavcodec


clean:
	rm -f vr
