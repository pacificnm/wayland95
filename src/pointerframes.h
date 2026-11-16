#pragma once

class QWaylandClient;

// Ends the pointer events just sent to `client` with wl_pointer.frame on each
// of its version 5 pointers (see pointerframes.cpp: wl_seat is offered at
// version 5, which Chromium needs to scroll).
void sendPointerFrames(QWaylandClient *client);
