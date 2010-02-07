#ifndef __VISIBILITY_H__
#define __VISIBILITY_H__

#define EXPORT_BY_DEFAULT _Pragma("GCC visibility push(default)")
#define HIDE_BY_DEFAULT   _Pragma("GCC visibility push(hidden)")

#define EXPORT __attribute__ ((visibility("default")))
#define HIDE   __attribute__ ((visibility("hidden")))

#endif /* __VISIBILITY_H__ */
