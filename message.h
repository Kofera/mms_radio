#ifndef CT_MSG_H
#define CT_MSG_H

/* Message types */
/** standard messages */
#define CT_MSG_INFO  0
/** error messages */
#define CT_MSG_ERR   1
/** warning messages */
#define CT_MSG_WARN  2
/** debug messages */
#define CT_MSG_DBG   3


void msg_Generic(void *, int, const char *, const char *, ...);
void msg_GenericVa(void *, int, const char *, const char *, va_list args);



#define msg_Info(p_this, ...) \
        msg_Generic(0, CT_MSG_INFO, \
                       p_this->psz_access, __VA_ARGS__)

#define msg_Err(p_this, ...) \
        msg_Generic(0, CT_MSG_ERR, \
                       p_this->psz_access, __VA_ARGS__)
        
#define msg_Warn(p_this, ...) \
        msg_Generic(0, CT_MSG_WARN, \
                       p_this->psz_access, __VA_ARGS__)
        
#define msg_Dbg(p_this, ...) \
        msg_Generic(0, CT_MSG_DBG, \
                       p_this->psz_access, __VA_ARGS__)

#endif
