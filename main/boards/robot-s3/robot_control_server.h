#ifndef ROBOT_CONTROL_SERVER_H
#define ROBOT_CONTROL_SERVER_H

void StartRobotControlServer();
bool RobotDrive(int left, int right);
bool RobotSetServo(const char* name, int value);
bool RobotSetLights(bool on, int r, int g, int b);
void RecordLearningMessage(const char* role, const char* content);

#endif
