/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2012 HPCC Systems.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
############################################################################## */

#ifndef _THORMISC_
#define _THORMISC_

#include "jiface.hpp"
#include "jthread.hpp"
#include "jexcept.hpp"
#include "jarray.hpp"
#include "jfile.hpp"
#include "jprop.hpp"
#include "jutil.hpp"
#include "jlog.hpp"
#include "mpcomm.hpp"

#include "workunit.hpp"
#include "eclhelper.hpp"
#include "thexception.hpp"
#include "thorcommon.hpp"
#include "thor.hpp"


#ifdef _WIN32
    #ifdef GRAPH_EXPORTS
        #define graph_decl __declspec(dllexport)
    #else
        #define graph_decl __declspec(dllimport)
    #endif
#else
    #define graph_decl
#endif

/// Thor options, that can be hints, workunit options, or global settings
#define THOROPT_COMPRESS_SPILLS       "compressInternalSpills"
#define THOROPT_HDIST_SPILL           "hdistSpill"
#define THOROPT_HDIST_WRITE_POOL_SIZE "hdistSendPoolSize"
#define THOROPT_SPLITTER_SPILL        "splitterSpill"
#define THOROPT_LOOP_MAX_EMPTY        "loopMaxEmpty"

#define INITIAL_SELFJOIN_MATCH_WARNING_LEVEL 20000  // max of row matches before selfjoin emits warning

#define THOR_SEM_RETRY_TIMEOUT 2
#define THOR_TRACE_LEVEL 5

enum ThorExceptionAction { tea_null, tea_warning, tea_abort, tea_shutdown };

enum RegistryCode { rc_register, rc_deregister };

#define createThorRow(size)         malloc(size)
#define destroyThorRow(ptr)         free(ptr)
#define reallocThorRow(ptr, size)   realloc(ptr, size)

class BooleanOnOff
{
    bool &tf;
public:
    inline BooleanOnOff(bool &_tf) : tf(_tf) { tf = true; }
    inline ~BooleanOnOff() { tf = false; }
};

class graph_decl CTimeoutTrigger : public CInterface, implements IThreaded
{
    bool running;
    Semaphore todo;
    CriticalSection crit;
    unsigned timeout;
    StringAttr description;
    CThreaded threaded;
protected:
    Owned<IException> exception;

public:
    CTimeoutTrigger(unsigned _timeout, const char *_description) : timeout(_timeout), description(_description), threaded("TimeoutTrigger")
    {
        running = (timeout!=0);
        threaded.init(this);
    }
    virtual ~CTimeoutTrigger()
    {
        stop();
        threaded.join();
    }
    void main()
    {
        while (running)
        {
            todo.wait(1000);
            CriticalBlock block(crit);
            if (exception.get())
            {
                { CriticalUnblock b(crit);
                    if (todo.wait(timeout*1000))
                    { // if signalled during timeout period, wait full timeout
                        if (running)
                            todo.wait(timeout*1000);
                    }
                }
                if (!running) break;
                if (exception.get())
                    if (action())
                        break;
            }
        }
    }
    void stop() { running = false; todo.signal(); }
    void inform(IException *e)
    {
        LOG(MCdebugProgress, unknownJob, "INFORM [%s]", description.get());
        CriticalBlock block(crit);
        if (exception.get())
            e->Release();
        else
        {
            exception.setown(e);
            todo.signal();
        }
    }
    IException *clear()
    {
        CriticalBlock block(crit);
        IException *e = exception.getClear();
        if (e)
            LOG(MCdebugProgress, unknownJob, "CLEARING TIMEOUT [%s]", description.get());
        todo.signal();
        return e;
    }
    virtual bool action() = 0;
};

// simple class which takes ownership of the underlying file and deletes it on destruction
class graph_decl CFileOwner : public CSimpleInterface, implements IInterface
{
    IFile *iFile;
public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);
    CFileOwner(IFile *_iFile) : iFile(_iFile)
    {
    }
    ~CFileOwner()
    {
        iFile->remove();
    }
    IFile &queryIFile() const { return *iFile; }
};

// stream wrapper, that takes ownership of a CFileOwner
class graph_decl CStreamFileOwner : public CSimpleInterface, implements IExtRowStream
{
    Linked<CFileOwner> fileOwner;
    IExtRowStream *stream;
public:
    IMPLEMENT_IINTERFACE_USING(CSimpleInterface);
    CStreamFileOwner(CFileOwner *_fileOwner, IExtRowStream *_stream) : fileOwner(_fileOwner)
    {
        stream = LINK(_stream);
    }
    ~CStreamFileOwner()
    {
        stream->Release();
    }
// IExtRowStream
    virtual const void *nextRow() { return stream->nextRow(); }
    virtual void stop() { stream->stop(); }
    virtual offset_t getOffset() { return stream->getOffset(); }
    virtual void stop(CRC32 *crcout=NULL) { stream->stop(); }
    virtual const void *prefetchRow(size32_t *sz=NULL) { return stream->prefetchRow(sz); }
    virtual void prefetchDone() { stream->prefetchDone(); }
    virtual void reinit(offset_t offset, offset_t len, unsigned __int64 maxRows)
    {
        stream->reinit(offset, len, maxRows);
    }
};


#define DEFAULT_QUERYSO_LIMIT 10

class graph_decl CFifoFileCache : public CSimpleInterface
{
    unsigned limit;
    StringArray files;
    void deleteFile(IFile &ifile);

public:
    void init(const char *cacheDir, unsigned _limit, const char *pattern);
    void add(const char *filename);
    bool isAvailable(const char *filename);
};

interface IBarrierException : extends IException {};
extern graph_decl IBarrierException *createBarrierAbortException();

interface IThorException : extends IException
{
    virtual ThorExceptionAction queryAction() = 0;
    virtual ThorActivityKind queryActivityKind() = 0;
    virtual activity_id queryActivityId() = 0;
    virtual graph_id queryGraphId() = 0;
    virtual const char *queryJobId() = 0;
    virtual void getAssert(StringAttr &file, unsigned &line, unsigned &column) = 0;
    virtual const char *queryOrigin() = 0;
    virtual WUExceptionSeverity querySeverity() = 0;
    virtual const char *queryMessage() = 0;
    virtual bool queryNotified() const = 0;
    virtual MemoryBuffer &queryData() = 0;
    virtual void setNotified() = 0;
    virtual void setAction(ThorExceptionAction _action) = 0;
    virtual void setActivityKind(ThorActivityKind _kind) = 0;
    virtual void setActivityId(activity_id id) = 0;
    virtual void setGraphId(graph_id id) = 0;
    virtual void setJobId(const char *jobId) = 0;
    virtual void setAudience(MessageAudience audience) = 0;
    virtual void setSlave(unsigned slave) = 0;
    virtual void setMessage(const char *msg) = 0;
    virtual void setAssert(const char *file, unsigned line, unsigned column) = 0;
    virtual void setOrigin(const char *origin) = 0;
    virtual void setSeverity(WUExceptionSeverity severity) = 0;
};

class CGraphElementBase;
class CActivityBase;
class CGraphBase;
interface IRemoteConnection;
enum ActLogEnum { thorlog_null=0,thorlog_ecl=1,thorlog_all=2 };

extern graph_decl StringBuffer &ActPrintLogArgsPrep(StringBuffer &res, const CGraphElementBase *container, const ActLogEnum flags, const char *format, va_list args);
extern graph_decl void ActPrintLogEx(const CGraphElementBase *container, const ActLogEnum flags, const LogMsgCategory &logCat, const char *format, ...) __attribute__((format(printf, 4, 5)));
extern graph_decl void ActPrintLogArgs(const CGraphElementBase *container, const ActLogEnum flags, const LogMsgCategory &logCat, const char *format, va_list args);
extern graph_decl void ActPrintLogArgs(const CGraphElementBase *container, IException *e, const ActLogEnum flags, const LogMsgCategory &logCat, const char *format, va_list args);
extern graph_decl void ActPrintLog(const CActivityBase *activity, const char *format, ...) __attribute__((format(printf, 2, 3)));
extern graph_decl void ActPrintLog(const CActivityBase *activity, IException *e, const char *format, ...) __attribute__((format(printf, 3, 4)));

inline void ActPrintLog(const CGraphElementBase *container, const char *format, ...) __attribute__((format(printf, 2, 3)));
inline void ActPrintLog(const CGraphElementBase *container, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    ActPrintLogArgs(container, thorlog_ecl, MCdebugProgress, format, args);
    va_end(args);
}
inline void ActPrintLogEx(const CGraphElementBase *container, IException *e, const ActLogEnum flags, const LogMsgCategory &logCat, const char *format, ...) __attribute__((format(printf, 5, 6)));
inline void ActPrintLogEx(const CGraphElementBase *container, IException *e, const ActLogEnum flags, const LogMsgCategory &logCat, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    ActPrintLogArgs(container, e, flags, logCat, format, args);
    va_end(args);
}
inline void ActPrintLog(const CGraphElementBase *container, IException *e, const char *format, ...) __attribute__((format(printf, 3, 4)));
inline void ActPrintLog(const CGraphElementBase *container, IException *e, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    ActPrintLogArgs(container, e, thorlog_null, MCexception(e, MSGCLS_error), format, args);
    va_end(args);
}
extern graph_decl void GraphPrintLogArgsPrep(StringBuffer &res, CGraphBase *graph, const ActLogEnum flags, const LogMsgCategory &logCat, const char *format, va_list args);
extern graph_decl void GraphPrintLogArgs(CGraphBase *graph, IException *e, const ActLogEnum flags, const LogMsgCategory &logCat, const char *format, va_list args);
extern graph_decl void GraphPrintLogArgs(CGraphBase *graph, const ActLogEnum flags, const LogMsgCategory &logCat, const char *format, va_list args);
extern graph_decl void GraphPrintLog(CGraphBase *graph, IException *e, const char *format, ...) __attribute__((format(printf, 3, 4)));

inline void GraphPrintLogEx(CGraphBase *graph, ActLogEnum flags, const LogMsgCategory &logCat, const char *format, ...) __attribute__((format(printf, 4, 5)));
inline void GraphPrintLogEx(CGraphBase *graph, ActLogEnum flags, const LogMsgCategory &logCat, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    GraphPrintLogArgs(graph, flags, logCat, format, args);
    va_end(args);
}
inline void GraphPrintLogEx(CGraphBase *graph, IException *e, ActLogEnum flags, const LogMsgCategory &logCat, const char *format, ...) __attribute__((format(printf, 5, 6)));
inline void GraphPrintLogEx(CGraphBase *graph, IException *e, ActLogEnum flags, const LogMsgCategory &logCat, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    GraphPrintLogArgs(graph, e, flags, logCat, format, args);
    va_end(args);
}
inline void GraphPrintLog(CGraphBase *graph, const char *format, ...) __attribute__((format(printf, 2, 3)));
inline void GraphPrintLog(CGraphBase *graph, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    GraphPrintLogArgs(graph, thorlog_null, MCdebugProgress, format, args);
    va_end(args);
}
extern graph_decl IThorException *MakeActivityException(CActivityBase *activity, int code, const char *_format, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *MakeActivityException(CActivityBase *activity, IException *e, const char *xtra=NULL, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *MakeActivityWarning(CActivityBase *activity, int code, const char *_format, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *MakeActivityWarning(CActivityBase *activity, IException *e, const char *format, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *MakeActivityException(CGraphElementBase *activity, int code, const char *_format, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *MakeActivityException(CGraphElementBase *activity, IException *e, const char *xtra=NULL, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *MakeActivityWarning(CGraphElementBase *activity, int code, const char *_format, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *MakeActivityWarning(CGraphElementBase *activity, IException *e, const char *format, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *MakeGraphException(CGraphBase *graph, int code, const char *format, ...);
extern graph_decl IThorException *MakeThorException(int code, const char *format, ...) __attribute__((format(printf, 2, 3)));
extern graph_decl IThorException *MakeThorException(IException *e);
extern graph_decl IThorException *MakeThorAudienceException(LogMsgAudience audience, int code, const char *format, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *MakeThorOperatorException(int code, const char *format, ...) __attribute__((format(printf, 2, 3)));
extern graph_decl IThorException *MakeThorFatal(IException *e, int code, const char *format, ...) __attribute__((format(printf, 3, 4)));
extern graph_decl IThorException *ThorWrapException(IException *e, const char *msg, ...) __attribute__((format(printf, 2, 3)));
extern graph_decl void setExceptionActivityInfo(CGraphElementBase &container, IThorException *e);

extern graph_decl void GetTempName(StringBuffer &name, const char *prefix=NULL,bool altdisk=false);
extern graph_decl void SetTempDir(const char *name,bool clear);
extern graph_decl void ClearDir(const char *dir);
extern graph_decl void ClearTempDirs();
extern graph_decl const char *queryTempDir(bool altdisk=false);  
extern graph_decl void loadCmdProp(IPropertyTree *tree, const char *cmdProp);

extern graph_decl void ensureDirectoryForFile(const char *fName);
extern graph_decl void reportExceptionToWorkunit(IConstWorkUnit &workunit,IException *e, WUExceptionSeverity severity=ExceptionSeverityWarning);

extern graph_decl IPropertyTree *globals;
extern graph_decl mptag_t masterSlaveMpTag;
enum SlaveMsgTypes { smt_errorMsg=1, smt_initGraphReq, smt_initActDataReq, smt_dataReq, smt_getPhysicalName, smt_getFileOffset, smt_actMsg, smt_getresult };
// Logging
extern graph_decl const LogMsgJobInfo thorJob;

extern graph_decl memsize_t queryLargeMemSize();
extern graph_decl StringBuffer &getCompoundQueryName(StringBuffer &compoundName, const char *queryName, unsigned version);

extern graph_decl void setClusterGroup(IGroup *group);
extern graph_decl bool clusterInitialized();
extern graph_decl ICommunicator &queryClusterComm();
extern graph_decl IGroup &queryClusterGroup();
extern graph_decl IGroup &querySlaveGroup();
extern graph_decl IGroup &queryDfsGroup();
extern graph_decl unsigned queryClusterWidth();
extern graph_decl unsigned queryClusterNode();

extern graph_decl mptag_t allocateClusterMPTag();     // should probably move into so used by master only
extern graph_decl void freeClusterMPTag(mptag_t tag); // ""

extern graph_decl IThorException *deserializeThorException(MemoryBuffer &in); 
void graph_decl serializeThorException(IException *e, MemoryBuffer &out); 

class CActivityBase;
interface IPartDescriptor;
extern graph_decl bool getBestFilePart(CActivityBase *activity, IPartDescriptor &partDesc, OwnedIFile & ifile, unsigned &location, StringBuffer &path, IExceptionHandler *eHandler = NULL);
extern graph_decl StringBuffer &getFilePartLocations(IPartDescriptor &partDesc, StringBuffer &locations);
extern graph_decl StringBuffer &getPartFilename(IPartDescriptor &partDesc, unsigned copy, StringBuffer &filePath, bool localMount=false);

extern graph_decl IOutputMetaData *createFixedSizeMetaData(size32_t sz);


interface IRowServer : extends IInterface
{
    virtual void stop() = 0;
};
extern graph_decl IRowStream *createRowStreamFromNode(CActivityBase &activity, unsigned node, ICommunicator &comm, mptag_t mpTag, const bool &abortSoon);
extern graph_decl IRowServer *createRowServer(CActivityBase *activity, IRowStream *seq, ICommunicator &comm, mptag_t mpTag);

extern graph_decl IRowStream *createUngroupStream(IRowStream *input);

interface IRowInterfaces;
extern graph_decl void sendInChunks(ICommunicator &comm, rank_t dst, mptag_t mpTag, IRowStream *input, IRowInterfaces *rowIf);

#endif

