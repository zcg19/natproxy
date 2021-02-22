#pragma once


// script call.
extern void * g_ls;
void * Script_Init(const char * szPath);
void   Script_Uninit(void * s);
void   Script_OnRecvXdcMsg(void * s, void * szData, int nLen);
void   Script_OnSendXdcMsg(void * s, void * szData, int nLen);
