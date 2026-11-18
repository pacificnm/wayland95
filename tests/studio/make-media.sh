#!/bin/bash
# Makes the media the MediaPlayer's tests play, in DIR: a second of a tone
# as MP3, WAV, FLAC, OGG and M4A, and a second of a test picture (with the
# tone) as MP4 (H.264) and WebM (VP9). Needs ffmpeg.
#   tests/studio/make-media.sh DIR
set -e
dir=$1
mkdir -p "$dir"
tone="-f lavfi -i sine=frequency=440:duration=1"
pic="-f lavfi -i testsrc=size=160x120:rate=15:duration=1"
q="-hide_banner -loglevel error -y"
ffmpeg $q $tone -c:a libmp3lame "$dir/tone.mp3"
ffmpeg $q $tone -c:a pcm_s16le "$dir/tone.wav"
ffmpeg $q $tone -c:a flac "$dir/tone.flac"
ffmpeg $q $tone -c:a libvorbis "$dir/tone.ogg"
ffmpeg $q $tone -c:a aac "$dir/tone.m4a"
ffmpeg $q $pic $tone -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$dir/clip.mp4"
ffmpeg $q $pic $tone -c:v libvpx-vp9 -c:a libopus -shortest "$dir/clip.webm"
