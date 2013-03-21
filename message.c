#include "common.h"
#include "message.h"

void msg_Generic(void *p_this, int i_type, const char *psz_module, 
                        const char *psz_format, ...)
{
     va_list args;
     
     va_start(args, psz_format);
     msg_GenericVa(p_this, i_type, psz_module, psz_format, args);
     va_end(args);
}

#define MSG_BUF_SZ 512

void msg_GenericVa(void *p_this, int i_type, 
                        const char *psz_module, 
                        const char *psz_format, va_list _args)
{   
    char buffer[MSG_BUF_SZ];
    memset(buffer, 0, sizeof buffer);
     
     switch(i_type)
     {
     case CT_MSG_INFO :
          printf("[MSG_INFO] module %s :", psz_module);    
          break;
          
     case CT_MSG_ERR :
          printf("[MSG_ERR] module %s :", psz_module);
          break;
          
     case CT_MSG_WARN :
          printf("[MSG_WARN] module %s :", psz_module);
          break;
          
     case CT_MSG_DBG :
#ifdef NO_DEBUG
          return ;
#endif
          printf("[MSG_DBG] module %s :", psz_module);
     }
     
     vsprintf(buffer, psz_format, _args);    
     printf("%s\n", buffer);
}
