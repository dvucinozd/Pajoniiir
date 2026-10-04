#pragma once
enum { SERVICE_LOG_RECORDING_RECOVERED, SERVICE_LOG_WARN };
void service_log_event(int,int,unsigned,unsigned,unsigned,unsigned,unsigned,const char *);
