#define _GNU_SOURCE
#include "native_log_store.h"
#include "native_log_backend.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
 char dir[]="build/journal-smoke-XXXXXX"; assert(mkdtemp(dir));
 char active[512]; assert(snprintf(active,sizeof(active),"%s/shared-runtime.log",dir)>0);
 const NlsBackend* backend=NULL; assert(linklab_native_fs_acquire(&backend)==0);
 NativeLogStore store; assert(nls_init(&store,active,backend)==0);
 const unsigned char first[]="{\"shared_runtime_fixture\":1}\n";
 const unsigned char next[]="{\"shared_runtime_fixture\":1}\n{\"append\":2}\n";
 assert(nls_commit(&store,first,sizeof(first)-1,false)==NLS_STABLE);
 assert(nls_commit(&store,next,sizeof(next)-1,false)==NLS_STABLE);
 assert(nls_recover(&store)==NLS_STABLE);
 unsigned char data[512];size_t len=0; assert(nls_read_active(&store,data,sizeof(data),&len)==0);
 assert(len==sizeof(next)-1 && !memcmp(data,next,len));
 assert(linklab_native_fs_release()==0);assert(unlink(active)==0);assert(rmdir(dir)==0);
 puts("PASS shared runtime fixture only: real journal create/append/recovery; NOT an implemented app or device test");
 return 0;
}
