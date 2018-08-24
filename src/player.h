#ifndef myPlay_H_
#define myPlay_H_
#ifdef USER_LIBCCHIP
#include <libcchip/platform.h>	
#include <libcchip/function/back_trace/back_trace.h>
#define myPlayLogIint(x...) log_init(x)
#define myPlayRaw(x...) trc(x)
#define myPlayWar(x...) war(x)
#define myPlayInf(x...) inf(x)
#define myPlayErr(x...) err(x)
#define myPlayDbg(x...) dbg(x)
#define myPlayTrc(x...) trc(x)
#define myPlayPerr(err,x...) show_errno(err,x)	
#else
#define myPlayLogIint(x...)
#define myPlayRaw(x...)
#define myPlayWar(x...)
#define myPlayInf(x...)
#define myPlayErr(x...)
#define myPlayDbg(x...)
#define myPlayTrc(x...)
#define traceSig(x...)
#define myPlayPerr(x...)
	
#define showProcessThreadId(msg)	
#define assert_arg(count,errCode){\
	int err=0;\
	if(!((argv[count]&&argv[count][0]))){\
		myPlayErr("arg is can't less %d",count);\
		return errCode;\
	}\
}

#endif	




typedef struct handle_item_t{
	const char *name;
	int (*handle)(char *argv[]);
	void *args;
}handle_item_t;

#endif
