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

#include "platform.h"
#include "jliball.hpp"
#include "eclrtl.hpp"
#include "eclhelper.hpp"
#include "rtlds_imp.hpp"
#include "rtlread_imp.hpp"

#define FIRST_CHUNK_SIZE  0x100
#define DOUBLE_LIMIT      0x10000           // must be a power of 2

unsigned getNextSize(unsigned max, unsigned required)
{
    if (required > DOUBLE_LIMIT)
    {
        max = (required + DOUBLE_LIMIT) & ~(DOUBLE_LIMIT-1);
        if (required >= max)
            throw MakeStringException(-1, "getNextSize: Request for %d bytes oldMax = %d", required, max);
    }
    else
    {
        if (max == 0)
            max = FIRST_CHUNK_SIZE;
        while (required >= max)
            max += max;
    }
    return max;
}

//---------------------------------------------------------------------------

RtlDatasetBuilder::RtlDatasetBuilder()
{
    maxSize = 0;
    buffer = NULL;
    totalSize = 0;
}


RtlDatasetBuilder::~RtlDatasetBuilder()
{
    free(buffer);
}

void RtlDatasetBuilder::ensure(size32_t required)
{
    if (required > maxSize)
    {
        maxSize = getNextSize(maxSize, required);
        buffer = (byte *)realloc(buffer, maxSize);
        if (!buffer)
            throw MakeStringException(-1, "Failed to allocate temporary dataset (requesting %d bytes)", maxSize);
    }
}

byte * RtlDatasetBuilder::ensureCapacity(size32_t required, const char * fieldName)
{
    ensure(totalSize + required);
    return buffer + totalSize;
}

void RtlDatasetBuilder::flushDataset()
{
}

void RtlDatasetBuilder::getData(size32_t & len, void * & data)
{
    flushDataset();
    len = totalSize;
    data = malloc(totalSize);
    memcpy(data, buffer, totalSize);
}


size32_t RtlDatasetBuilder::getSize()
{
    flushDataset();
    return totalSize;
}

byte * RtlDatasetBuilder::queryData()
{
    flushDataset();
    return buffer;
}

void RtlDatasetBuilder::reportMissingRow() const
{
    throw MakeStringException(MSGAUD_user, 1000, "RtlDatasetBuilder::row() is NULL");
}



//---------------------------------------------------------------------------

RtlFixedDatasetBuilder::RtlFixedDatasetBuilder(unsigned _recordSize, unsigned maxRows)
{
    recordSize = _recordSize;
    if ((int)maxRows > 0)
        ensure(recordSize * maxRows);
}


byte * RtlFixedDatasetBuilder::createSelf()
{
    self = ensureCapacity(recordSize, NULL);
    return self;
}

//---------------------------------------------------------------------------

RtlLimitedFixedDatasetBuilder::RtlLimitedFixedDatasetBuilder(unsigned _recordSize, unsigned _maxRows, DefaultRowCreator _rowCreator, IResourceContext *_ctx) : RtlFixedDatasetBuilder(_recordSize, _maxRows)
{
    maxRows = _maxRows;
    if ((int)maxRows < 0) maxRows = 0;
    rowCreator = _rowCreator;
    ctx = _ctx;
}


byte * RtlLimitedFixedDatasetBuilder::createRow()
{
    if (totalSize >= maxRows * recordSize)
        return NULL;
    return RtlFixedDatasetBuilder::createRow();
}


void RtlLimitedFixedDatasetBuilder::flushDataset()
{
    if (rowCreator)
    {
        while (totalSize < maxRows * recordSize)
        {
            createRow();
            size32_t size = rowCreator(rowBuilder(), ctx);
            finalizeRow(size);
        }
    }
    RtlFixedDatasetBuilder::flushDataset();
}

//---------------------------------------------------------------------------

RtlVariableDatasetBuilder::RtlVariableDatasetBuilder(IRecordSize & _recordSize)
{
    recordSize = &_recordSize;
    maxRowSize = recordSize->getRecordSize(NULL); // initial size
}


byte * RtlVariableDatasetBuilder::createSelf()
{
    self = ensureCapacity(maxRowSize, NULL);
    return self;
}


void RtlVariableDatasetBuilder::deserializeRow(IOutputRowDeserializer & deserializer, IRowDeserializerSource & in)
{
    createRow();
    size32_t rowSize = deserializer.deserialize(rowBuilder(), in);
    finalizeRow(rowSize);
}



//---------------------------------------------------------------------------

RtlLimitedVariableDatasetBuilder::RtlLimitedVariableDatasetBuilder(IRecordSize & _recordSize, unsigned _maxRows, DefaultRowCreator _rowCreator, IResourceContext *_ctx) : RtlVariableDatasetBuilder(_recordSize)
{
    numRows = 0;
    maxRows = _maxRows;
    rowCreator = _rowCreator;
    ctx = _ctx;
}



byte * RtlLimitedVariableDatasetBuilder::createRow()
{
    if (numRows >= maxRows)
        return NULL;
    numRows++;
    return RtlVariableDatasetBuilder::createRow();
}


void RtlLimitedVariableDatasetBuilder::flushDataset()
{
    if (rowCreator)
    {
        while (numRows < maxRows)
        {
            createRow();
            size32_t thisSize = rowCreator(rowBuilder(), ctx);
            finalizeRow(thisSize);
        }
    }
    RtlVariableDatasetBuilder::flushDataset();
}


//---------------------------------------------------------------------------

byte * * rtlRowsAttr::linkrows() const      
{ 
    if (rows)
        rtlLinkRowset(rows);
    return rows;
}

void rtlRowsAttr::set(size32_t _count, byte * * _rows)
{
    setown(_count, rtlLinkRowset(_rows));
}

void rtlRowsAttr::setRow(IEngineRowAllocator * rowAllocator, const byte * _row)
{
    setown(1, rowAllocator->appendRowOwn(NULL, 1, rowAllocator->linkRow(_row)));
}

void rtlRowsAttr::setown(size32_t _count, byte * * _rows)
{
    dispose();
    count = _count;
    rows = _rows;
}

void rtlRowsAttr::dispose()
{
    if (rows)
        rtlReleaseRowset(count, rows);
}

//---------------------------------------------------------------------------

void rtlReportFieldOverflow(unsigned size, unsigned max, const RtlFieldInfo * field)
{
    if (field)
        rtlReportFieldOverflow(size, max, field->name->str());
    else
        rtlReportRowOverflow(size, max);
}


void RtlRowBuilderBase::reportMissingRow() const
{
    throw MakeStringException(MSGAUD_user, 1000, "RtlRowBuilderBase::row() is NULL");
}

byte * RtlDynamicRowBuilder::ensureCapacity(size32_t required, const char * fieldName)
{
    if (required > maxLength)
    {
        if (!self)
            create();

        if (required > maxLength)
        {
            void * next = rowAllocator->resizeRow(required, self, maxLength);
            if (!next)
            {
                rtlReportFieldOverflow(required, maxLength, fieldName);
                return NULL;
            }
            self = static_cast<byte *>(next);
        }
    }
    return self;
}

void RtlDynamicRowBuilder::swapWith(RtlDynamicRowBuilder & other)
{
    size32_t savedMaxLength = maxLength;
    void * savedSelf = getUnfinalizedClear();
    setown(other.getMaxLength(), other.getUnfinalizedClear());
    other.setown(savedMaxLength, savedSelf);
}


//---------------------------------------------------------------------------

byte * RtlStaticRowBuilder::ensureCapacity(size32_t required, const char * fieldName)
{
    if (required <= maxLength)
        return static_cast<byte *>(self);
    rtlReportFieldOverflow(required, maxLength, fieldName);
    return NULL;
}

byte * RtlStaticRowBuilder::createSelf() 
{ 
    throwUnexpected();
}

//---------------------------------------------------------------------------

RtlLinkedDatasetBuilder::RtlLinkedDatasetBuilder(IEngineRowAllocator * _rowAllocator, int _choosenLimit) : builder(_rowAllocator, false)
{
    rowAllocator = LINK(_rowAllocator);
    rowset = NULL;
    count = 0;
    max = 0;
    choosenLimit = (unsigned)_choosenLimit;
}

RtlLinkedDatasetBuilder::~RtlLinkedDatasetBuilder()
{
    builder.clear();
    if (rowset)
        rowAllocator->releaseRowset(count, rowset);
    ::Release(rowAllocator);
}

void RtlLinkedDatasetBuilder::clear()
{
    builder.clear();
    if (rowset)
        rowAllocator->releaseRowset(count, rowset);
    rowset = NULL;
    count = 0;
    max = 0;
}

void RtlLinkedDatasetBuilder::append(const void * source)
{
    if (count < choosenLimit)
    {
        ensure(count+1);
        rowset[count] = source ? (byte *)rowAllocator->linkRow(source) : NULL;
        count++;
    }
}

void RtlLinkedDatasetBuilder::appendRows(size32_t num, byte * * rows)
{
    if (num && (count < choosenLimit))
    {
        unsigned maxNumToAdd = (count + num < choosenLimit) ? num : choosenLimit - count;
        unsigned numAdded = 0;
        ensure(count+maxNumToAdd);
        for (unsigned i=0; i < num; i++)
        {
            byte *row = rows[i];
            if (row)
            {
                rowset[count+numAdded] = (byte *)rowAllocator->linkRow(row);
                numAdded++;
                if (numAdded == maxNumToAdd)
                    break;
            }
        }
        count += numAdded;
    }
}

void RtlLinkedDatasetBuilder::appendOwn(const void * row)
{
    assertex(!builder.exists());
    if (count < choosenLimit)
    {
        ensure(count+1);
        rowset[count] = (byte *)row;
        count++;
    }
    else
        rowAllocator->releaseRow(row);
}


byte * RtlLinkedDatasetBuilder::createRow()
{
    if (count >= choosenLimit)
        return NULL;
    return builder.getSelf();
}


//cloned from thorcommon.cpp
class RtlChildRowLinkerWalker : implements IIndirectMemberVisitor
{
public:
    virtual void visitRowset(size32_t count, byte * * rows)
    {
        rtlLinkRowset(rows);
    }
    virtual void visitRow(const byte * row)
    {
        rtlLinkRow(row);
    }
};


void RtlLinkedDatasetBuilder::cloneRow(size32_t len, const void * row)
{
    if (count >= choosenLimit)
        return;

    byte * self = builder.ensureCapacity(len, NULL);
    memcpy(self, row, len);
    
    IOutputMetaData * meta = rowAllocator->queryOutputMeta();
    if (meta->getMetaFlags() & MDFneedserialize)
    {
        RtlChildRowLinkerWalker walker;
        meta->walkIndirectMembers(self, walker);
    }

    finalizeRow(len);
}


void RtlLinkedDatasetBuilder::deserializeRow(IOutputRowDeserializer & deserializer, IRowDeserializerSource & in)
{
    builder.ensureRow();
    size32_t rowSize = deserializer.deserialize(builder, in);
    finalizeRow(rowSize);
}


inline void doDeserializeRowset(RtlLinkedDatasetBuilder & builder, IOutputRowDeserializer & deserializer, IRowDeserializerSource & in, offset_t marker, bool isGrouped)
{
    byte eogPending = false;
    while (!in.finishedNested(marker))
    {
        if (isGrouped && eogPending)
            builder.appendEOG();
        builder.deserializeRow(deserializer, in);
        if (isGrouped)
            in.read(1, &eogPending);
    }
}

inline void doSerializeRowset(IRowSerializerTarget & out, IOutputRowSerializer * serializer, size32_t count, byte * * rows, bool isGrouped)
{
    for (unsigned i=0; i < count; i++)
    {
        byte *row = rows[i];
        if (row)
        {
            serializer->serialize(out, rows[i]);
            if (isGrouped)
            {
                byte eogPending = (i+1 < count) && (rows[i+1] == NULL);
                out.put(1, &eogPending);
            }
        }
        else
        {
            assert(isGrouped); // should not be seeing NULLs otherwise - should not use this function for DICTIONARY
        }
    }
}


void RtlLinkedDatasetBuilder::deserialize(IOutputRowDeserializer & deserializer, IRowDeserializerSource & in, bool isGrouped)
{
    offset_t marker = in.beginNested();
    doDeserializeRowset(*this, deserializer, in, marker, isGrouped);
}


void RtlLinkedDatasetBuilder::finalizeRows()
{
    if (count != max)
        resize(count);
}


void RtlLinkedDatasetBuilder::finalizeRow(size32_t rowSize)
{
    assertex(builder.exists());
    const void * next = builder.finalizeRowClear(rowSize);
    appendOwn(next);
}

byte * * RtlLinkedDatasetBuilder::linkrows() 
{ 
    finalizeRows(); 
    return rtlLinkRowset(rowset);
}

void RtlLinkedDatasetBuilder::expand(size32_t required)
{
    assertex(required <= choosenLimit);
    //MORE: Next factoring change this so it passes this logic over to the row allocator
    size32_t newMax = max ? max : 4;
    while (newMax < required)
    {
        newMax += newMax;
        assertex(newMax);
    }
    if (newMax > choosenLimit)
        newMax = choosenLimit;
    resize(newMax);
}

void RtlLinkedDatasetBuilder::resize(size32_t required)
{
    rowset = rowAllocator->reallocRows(rowset, max, required);
    max = required;
}

void appendRowsToRowset(size32_t & targetCount, byte * * & targetRowset, IEngineRowAllocator * rowAllocator, size32_t extraCount, byte * * extraRows)
{
    if (extraCount)
    {
        size32_t prevCount = targetCount;
        byte * * expandedRowset = rowAllocator->reallocRows(targetRowset, prevCount, prevCount+extraCount);
        unsigned numAdded = 0;
        for (unsigned i=0; i < extraCount; i++)
        {
            byte *extraRow = extraRows[i];
            if (extraRow)
            {
                expandedRowset[prevCount+numAdded] = (byte *)rowAllocator->linkRow(extraRow);
                numAdded++;
            }
        }
        targetCount = prevCount + numAdded;
        targetRowset = expandedRowset;
    }
}


inline void doDeserializeRowset(size32_t & count, byte * * & rowset, IEngineRowAllocator * _rowAllocator, IOutputRowDeserializer * deserializer, IRowDeserializerSource & in, bool isGrouped)
{
    RtlLinkedDatasetBuilder builder(_rowAllocator);

    builder.deserialize(*deserializer, in, isGrouped);

    count = builder.getcount();
    rowset = builder.linkrows();
}


extern ECLRTL_API void rtlDeserializeRowset(size32_t & count, byte * * & rowset, IEngineRowAllocator * _rowAllocator, IOutputRowDeserializer * deserializer, IRowDeserializerSource & in)
{
    doDeserializeRowset(count, rowset, _rowAllocator, deserializer, in, false);
}


extern ECLRTL_API void rtlDeserializeGroupedRowset(size32_t & count, byte * * & rowset, IEngineRowAllocator * _rowAllocator, IOutputRowDeserializer * deserializer, IRowDeserializerSource & in)
{
    doDeserializeRowset(count, rowset, _rowAllocator, deserializer, in, true);
}


void rtlSerializeRowset(IRowSerializerTarget & out, IOutputRowSerializer * serializer, size32_t count, byte * * rows)
{
    size32_t marker = out.beginNested();
    doSerializeRowset(out, serializer, count, rows, false);
    out.endNested(marker);
}

void rtlSerializeGroupedRowset(IRowSerializerTarget & out, IOutputRowSerializer * serializer, size32_t count, byte * * rows)
{
    size32_t marker = out.beginNested();
    doSerializeRowset(out, serializer, count, rows, true);
    out.endNested(marker);
}

//---------------------------------------------------------------------------

RtlLinkedDictionaryBuilder::RtlLinkedDictionaryBuilder(IEngineRowAllocator * _rowAllocator, IHThorHashLookupInfo *_hashInfo, unsigned _initialSize)
  : builder(_rowAllocator, false)
{
    init(_rowAllocator, _hashInfo, _initialSize);
}

RtlLinkedDictionaryBuilder::RtlLinkedDictionaryBuilder(IEngineRowAllocator * _rowAllocator, IHThorHashLookupInfo *_hashInfo)
  : builder(_rowAllocator, false)
{
    init(_rowAllocator, _hashInfo, 8);
}

void RtlLinkedDictionaryBuilder::init(IEngineRowAllocator * _rowAllocator, IHThorHashLookupInfo *_hashInfo, unsigned _initialSize)
{
    hash  = _hashInfo->queryHash();
    compare  = _hashInfo->queryCompare();
    initialSize = _initialSize;
    rowAllocator = LINK(_rowAllocator);
    table = NULL;
    usedCount = 0;
    usedLimit = 0;
    tableSize = 0;
}

RtlLinkedDictionaryBuilder::~RtlLinkedDictionaryBuilder()
{
//    builder.clear();
    if (table)
        rowAllocator->releaseRowset(tableSize, table);
    ::Release(rowAllocator);
}

void RtlLinkedDictionaryBuilder::append(const void * source)
{
    if (source)
    {
        appendOwn(rowAllocator->linkRow(source));
    }
}

void RtlLinkedDictionaryBuilder::appendOwn(const void * source)
{
    if (source)
    {
        checkSpace();
        unsigned rowidx = hash->hash(source) % tableSize;
        loop
        {
            const void *entry = table[rowidx];
            if (entry && compare->docompare(source, entry)==0)
            {
                rowAllocator->releaseRow(entry);
                usedCount--;
                entry = NULL;
            }
            if (!entry)
            {
                table[rowidx] = (byte *) source;
                usedCount++;
                break;
            }
            rowidx++;
            if (rowidx==tableSize)
                rowidx = 0;
        }
    }
}

void RtlLinkedDictionaryBuilder::checkSpace()
{
    if (!table)
    {
        table = rowAllocator->createRowset(initialSize);
        tableSize = initialSize;
        memset(table, 0, tableSize*sizeof(byte *));
        usedLimit = (tableSize * 3) / 4;
        usedCount = 0;
    }
    else if (usedCount > usedLimit)
    {
        // Rehash
        byte * * oldTable = table;
        unsigned oldSize = tableSize;
        table = rowAllocator->createRowset(tableSize*2);
        tableSize = tableSize*2; // Don't update until we have successfully allocated, so that we remain consistent if createRowset throws an exception.
        memset(table, 0, tableSize * sizeof(byte *));
        usedLimit = (tableSize * 3) / 4;
        usedCount = 0;
        unsigned i;
        for (i = 0; i < oldSize; i++)
        {
            append(oldTable[i]);  // we link the rows here...
        }
        rowAllocator->releaseRowset(oldSize, oldTable);   // ... because this will release them
    }
}

void RtlLinkedDictionaryBuilder::appendRows(size32_t num, byte * * rows)
{
    // MORE - if we know that the source is already a hashtable, we can optimize the add to an empty table...
    for (unsigned i=0; i < num; i++)
        append(rows[i]);
}

void RtlLinkedDictionaryBuilder::finalizeRow(size32_t rowSize)
{
    assertex(builder.exists());
    const void * next = builder.finalizeRowClear(rowSize);
    appendOwn(next);
}

void RtlLinkedDictionaryBuilder::cloneRow(size32_t len, const void * row)
{
    byte * self = builder.ensureCapacity(len, NULL);
    memcpy(self, row, len);

    IOutputMetaData * meta = rowAllocator->queryOutputMeta();
    if (meta->getMetaFlags() & MDFneedserialize)
    {
        RtlChildRowLinkerWalker walker;
        meta->walkIndirectMembers(self, walker);
    }

    finalizeRow(len);
}

extern ECLRTL_API unsigned __int64 rtlDictionaryCount(size32_t tableSize, byte **table)
{
    unsigned __int64 ret = 0;
    for (size32_t i = 0; i < tableSize; i++)
        if (table[i])
            ret++;
    return ret;
}

extern ECLRTL_API byte *rtlDictionaryLookup(IHThorHashLookupInfo &hashInfo, size32_t tableSize, byte **table, const byte *source, byte *defaultRow)
{
    if (!tableSize)
        return (byte *) rtlLinkRow(defaultRow);

    IHash *hash  = hashInfo.queryHash();
    ICompare *compare  = hashInfo.queryCompare();
    unsigned rowidx = hash->hash(source) % tableSize;
    loop
    {
        const void *entry = table[rowidx];
        if (!entry)
            return (byte *) rtlLinkRow(defaultRow);
        if (compare->docompare(source, entry)==0)
            return (byte *) rtlLinkRow(entry);
        rowidx++;
        if (rowidx==tableSize)
            rowidx = 0;
    }
}

extern ECLRTL_API bool rtlDictionaryLookupExists(IHThorHashLookupInfo &hashInfo, size32_t tableSize, byte **table, const byte *source)
{
    if (!tableSize)
        return false;

    IHash *hash  = hashInfo.queryHash();
    ICompare *compare  = hashInfo.queryCompare();
    unsigned rowidx = hash->hash(source) % tableSize;
    loop
    {
        const void *entry = table[rowidx];
        if (!entry)
            return false;
        if (compare->docompare(source, entry)==0)
            return true;
        rowidx++;
        if (rowidx==tableSize)
            rowidx = 0;
    }
}

extern ECLRTL_API void rtlSerializeDictionary(IRowSerializerTarget & out, IOutputRowSerializer * serializer, size32_t count, byte * * rows)
{
    out.put(sizeof(count), &count);
    size32_t idx = 0;
    while (idx < count)
    {
        byte numRows = 0;
        while (numRows < 255 && idx+numRows < count && rows[idx+numRows] != NULL)
            numRows++;
        out.put(1, &numRows);
        for (int i = 0; i < numRows; i++)
        {
            byte *nextrec = rows[idx+i];
            assert(nextrec);
            serializer->serialize(out, nextrec);
        }
        idx += numRows;
        byte numNulls = 0;
        while (numNulls < 255 && idx+numNulls < count && rows[idx+numNulls] == NULL)
            numNulls++;
        out.put(1, &numNulls);
        idx += numNulls;
    }
}

extern ECLRTL_API void rtlDictionary2RowsetX(size32_t & count, byte * * & rowset, IEngineRowAllocator * rowAllocator, IOutputRowDeserializer * deserializer, size32_t lenSrc, const void * src)
{
    RtlLinkedDatasetBuilder builder(rowAllocator);
    Owned<ISerialStream> stream = createMemorySerialStream(src, lenSrc);
    CThorStreamDeserializerSource source(stream);

    size32_t totalRows;
    source.read(sizeof(totalRows), &totalRows);
    builder.ensure(totalRows);
    byte nullsPending = 0;
    byte rowsPending = 0;
    while (!source.finishedNested(lenSrc))
    {
        source.read(1, &rowsPending);
        for (int i = 0; i < rowsPending; i++)
            builder.deserializeRow(*deserializer, source);
        source.read(1, &nullsPending);
        for (int i = 0; i < nullsPending; i++)
            builder.append(NULL);
    }

    count = builder.getcount();
    assertex(count==totalRows);
    rowset = builder.linkrows();
}

//---------------------------------------------------------------------------

//These definitions should be shared with thorcommon, but to do that
//they would need to be moved to an rtlds.ipp header, which thorcommon then included.

class ECLRTL_API CThorRtlRowSerializer : implements IRowSerializerTarget
{
public:
    CThorRtlRowSerializer(MemoryBuffer & _buffer) : buffer(_buffer)
    {
    }

    virtual void put(size32_t len, const void * ptr)
    {
        buffer.append(len, ptr);
    }

    virtual size32_t beginNested()
    {
        unsigned pos = buffer.length();
        buffer.append((size32_t)0);
        return pos;
    }

    virtual void endNested(size32_t sizePos)
    {
        unsigned pos = buffer.length();
        buffer.rewrite(sizePos);
        buffer.append((size32_t)(pos - (sizePos + sizeof(size32_t))));
        buffer.rewrite(pos);
    }

protected:
    MemoryBuffer & buffer;
};


inline void doDataset2RowsetX(size32_t & count, byte * * & rowset, IEngineRowAllocator * rowAllocator, IOutputRowDeserializer * deserializer, size32_t lenSrc, const void * src, bool isGrouped)
{
    RtlLinkedDatasetBuilder builder(rowAllocator);
    Owned<ISerialStream> stream = createMemorySerialStream(src, lenSrc);
    CThorStreamDeserializerSource source(stream);

    doDeserializeRowset(builder, *deserializer, source, lenSrc, isGrouped);

    count = builder.getcount();
    rowset = builder.linkrows();
}

inline void doRowset2DatasetX(unsigned & tlen, void * & tgt, IOutputRowSerializer * serializer, size32_t count, byte * * rows, bool isGrouped)
{
    MemoryBuffer buffer;
    CThorRtlRowSerializer out(buffer);

    doSerializeRowset(out, serializer, count, rows, isGrouped);

    rtlFree(tgt);
    tlen = buffer.length();
    tgt = buffer.detach();      // not strictly speaking correct - it should have been allocated with rtlMalloc();
}

extern ECLRTL_API void rtlDataset2RowsetX(size32_t & count, byte * * & rowset, IEngineRowAllocator * rowAllocator, IOutputRowDeserializer * deserializer, size32_t lenSrc, const void * src, bool isGrouped)
{
    doDataset2RowsetX(count, rowset, rowAllocator, deserializer, lenSrc, src, isGrouped);
}

extern ECLRTL_API void rtlDataset2RowsetX(size32_t & count, byte * * & rowset, IEngineRowAllocator * rowAllocator, IOutputRowDeserializer * deserializer, size32_t lenSrc, const void * src)
{
    doDataset2RowsetX(count, rowset, rowAllocator, deserializer, lenSrc, src, false);
}

extern ECLRTL_API void rtlGroupedDataset2RowsetX(size32_t & count, byte * * & rowset, IEngineRowAllocator * rowAllocator, IOutputRowDeserializer * deserializer, size32_t lenSrc, const void * src)
{
    doDataset2RowsetX(count, rowset, rowAllocator, deserializer, lenSrc, src, true);
}

extern ECLRTL_API void rtlRowset2DatasetX(unsigned & tlen, void * & tgt, IOutputRowSerializer * serializer, size32_t count, byte * * rows, bool isGrouped)
{
    doRowset2DatasetX(tlen, tgt, serializer, count, rows, isGrouped);
}

extern ECLRTL_API void rtlRowset2DatasetX(unsigned & tlen, void * & tgt, IOutputRowSerializer * serializer, size32_t count, byte * * rows)
{
    doRowset2DatasetX(tlen, tgt, serializer, count, rows, false);
}

extern ECLRTL_API void rtlGroupedRowset2DatasetX(unsigned & tlen, void * & tgt, IOutputRowSerializer * serializer, size32_t count, byte * * rows)
{
    doRowset2DatasetX(tlen, tgt, serializer, count, rows, true);
}

extern ECLRTL_API void rtlRowset2DictionaryX(unsigned & tlen, void * & tgt, IOutputRowSerializer * serializer, size32_t count, byte * * rows)
{
    MemoryBuffer rowdata;
    CThorRtlRowSerializer out(rowdata);
    rtlSerializeDictionary(out, serializer, count, rows);

    rtlFree(tgt);
    tlen = rowdata.length();
    tgt = rowdata.detach();
}

void deserializeRowsetX(size32_t & count, byte * * & rowset, IEngineRowAllocator * _rowAllocator, IOutputRowDeserializer * deserializer, MemoryBuffer &in)
{
    Owned<ISerialStream> stream = createMemoryBufferSerialStream(in);
    CThorStreamDeserializerSource rowSource(stream);
    doDeserializeRowset(count, rowset, _rowAllocator, deserializer, rowSource, false);
}

void deserializeGroupedRowsetX(size32_t & count, byte * * & rowset, IEngineRowAllocator * _rowAllocator, IOutputRowDeserializer * deserializer, MemoryBuffer &in)
{
    Owned<ISerialStream> stream = createMemoryBufferSerialStream(in);
    CThorStreamDeserializerSource rowSource(stream);
    doDeserializeRowset(count, rowset, _rowAllocator, deserializer, rowSource, true);
}

void serializeRowsetX(size32_t count, byte * * rows, IOutputRowSerializer * serializer, MemoryBuffer & buffer)
{
    CThorRtlRowSerializer out(buffer);
    rtlSerializeRowset(out, serializer, count, rows);
}

void serializeGroupedRowsetX(size32_t count, byte * * rows, IOutputRowSerializer * serializer, MemoryBuffer & buffer)
{
    CThorRtlRowSerializer out(buffer);
    rtlSerializeGroupedRowset(out, serializer, count, rows);
}


void serializeRow(const void * row, IOutputRowSerializer * serializer, MemoryBuffer & buffer)
{
    CThorRtlRowSerializer out(buffer);
    serializer->serialize(out, static_cast<const byte *>(row));
}


extern ECLRTL_API byte * rtlDeserializeBufferRow(IEngineRowAllocator * rowAllocator, IOutputRowDeserializer * deserializer, MemoryBuffer & buffer)
{
    Owned<ISerialStream> stream = createMemoryBufferSerialStream(buffer);
    CThorStreamDeserializerSource source(stream);

    RtlDynamicRowBuilder rowBuilder(rowAllocator);
    size32_t rowSize = deserializer->deserialize(rowBuilder, source);
    return static_cast<byte *>(const_cast<void *>(rowBuilder.finalizeRowClear(rowSize)));
}


extern ECLRTL_API byte * rtlDeserializeRow(IEngineRowAllocator * rowAllocator, IOutputRowDeserializer * deserializer, const void * src)
{
    Owned<ISerialStream> stream = createMemorySerialStream(src, 0x7fffffff);
    CThorStreamDeserializerSource source(stream);

    RtlDynamicRowBuilder rowBuilder(rowAllocator);
    size32_t rowSize = deserializer->deserialize(rowBuilder, source);
    return static_cast<byte *>(const_cast<void *>(rowBuilder.finalizeRowClear(rowSize)));
}


extern ECLRTL_API size32_t rtlSerializeRow(size32_t lenOut, void * out, IOutputRowSerializer * serializer, const void * src)
{
    MemoryBuffer buffer;
    CThorRtlRowSerializer result(buffer);
    buffer.setBuffer(lenOut, out, false);
    buffer.setWritePos(0);
    serializer->serialize(result, (const byte *)src);
    return buffer.length();
}


class ECLRTL_API CThorRtlBuilderSerializer : implements IRowSerializerTarget
{
public:
    CThorRtlBuilderSerializer(ARowBuilder & _builder) : builder(_builder)
    {
        offset = 0;
    }

    virtual void put(size32_t len, const void * ptr)
    {
        byte * data = builder.ensureCapacity(offset + len, "");
        memcpy(data+offset, ptr, len);
        offset += len;
    }

    virtual size32_t beginNested()
    {
        unsigned pos = offset;
        offset += sizeof(size32_t);
        return pos;
    }

    virtual void endNested(size32_t sizePos)
    {
        byte * self = builder.getSelf();
        *(size32_t *)(self + sizePos) = offset - (sizePos + sizeof(size32_t));
    }

    inline size32_t length() const { return offset; }

protected:
    ARowBuilder & builder;
    size32_t offset;
};


extern ECLRTL_API size32_t rtlDeserializeToBuilder(ARowBuilder & builder, IOutputRowDeserializer * deserializer, const void * src)
{
    Owned<ISerialStream> stream = createMemorySerialStream(src, 0x7fffffff);
    CThorStreamDeserializerSource source(stream);
    return deserializer->deserialize(builder, source);
}

extern ECLRTL_API size32_t rtlSerializeToBuilder(ARowBuilder & builder, IOutputRowSerializer * serializer, const void * src)
{
    CThorRtlBuilderSerializer target(builder);
    serializer->serialize(target, (const byte *)src);
    return target.length();
}

//---------------------------------------------------------------------------

RtlDatasetCursor::RtlDatasetCursor(size32_t _len, const void * _data)
{
    setDataset(_len, _data);
}

bool RtlDatasetCursor::exists()
{
    return (end != buffer);
}

const byte * RtlDatasetCursor::first()
{
    if (buffer != end)
        cur = buffer;
    return cur;
}

const byte * RtlDatasetCursor::get()
{
    return cur;
}

void RtlDatasetCursor::setDataset(size32_t _len, const void * _data)
{
    buffer = (const byte *)_data;
    end = buffer + _len;
    cur = NULL;
}

bool RtlDatasetCursor::isValid()
{
    return (cur != NULL);
}

/*
const byte * RtlDatasetCursor::next()
{
    if (cur)
    {
        cur += getRowSize();
        if (cur >= end)
            cur = NULL;
    }
    return cur;
}

*/


//---------------------------------------------------------------------------

RtlFixedDatasetCursor::RtlFixedDatasetCursor(size32_t _len, const void * _data, unsigned _recordSize) : RtlDatasetCursor(_len, _data)
{
    recordSize = _recordSize;
}

RtlFixedDatasetCursor::RtlFixedDatasetCursor() : RtlDatasetCursor(0, NULL)
{
    recordSize = 1;
}

size32_t RtlFixedDatasetCursor::count()
{
    return (size32_t)((end - buffer) / recordSize);
}

size32_t RtlFixedDatasetCursor::getSize()
{
    return recordSize;
}

void RtlFixedDatasetCursor::init(size32_t _len, const void * _data, unsigned _recordSize)
{
    recordSize = _recordSize;
    setDataset(_len, _data);
}

const byte * RtlFixedDatasetCursor::next()
{
    if (cur)
    {
        cur += recordSize;
        if (cur >= end)
            cur = NULL;
    }
    return cur;
}

const byte * RtlFixedDatasetCursor::select(unsigned idx)
{
    cur = buffer + idx * recordSize;
    if (cur >= end)
        cur = NULL;
    return cur;
}


//---------------------------------------------------------------------------

RtlVariableDatasetCursor::RtlVariableDatasetCursor(size32_t _len, const void * _data, IRecordSize & _recordSize) : RtlDatasetCursor(_len, _data)
{
    recordSize = &_recordSize;
}

RtlVariableDatasetCursor::RtlVariableDatasetCursor() : RtlDatasetCursor(0, NULL)
{
    recordSize = NULL;
}

void RtlVariableDatasetCursor::init(size32_t _len, const void * _data, IRecordSize & _recordSize)
{
    recordSize = &_recordSize;
    setDataset(_len, _data);
}

size32_t RtlVariableDatasetCursor::count()
{
    const byte * finger = buffer;
    unsigned c = 0;
    while (finger < end)
    {
        finger += recordSize->getRecordSize(finger);
        c++;
    }
    assertex(finger == end);
    return c;
}

size32_t RtlVariableDatasetCursor::getSize()
{
    return recordSize->getRecordSize(cur);
}

const byte * RtlVariableDatasetCursor::next()
{
    if (cur)
    {
        cur += recordSize->getRecordSize(cur);
        if (cur >= end)
            cur = NULL;
    }
    return cur;
}

const byte * RtlVariableDatasetCursor::select(unsigned idx)
{
    const byte * finger = buffer;
    unsigned c = 0;
    while (finger < end)
    {
        if (c == idx)
        {
            cur = finger;
            return cur;
        }
        finger += recordSize->getRecordSize(finger);
        c++;
    }
    assertex(finger == end);
    cur = NULL;
    return NULL;
}

//---------------------------------------------------------------------------

RtlLinkedDatasetCursor::RtlLinkedDatasetCursor(unsigned _numRows, byte * * _rows) : numRows(_numRows), rows(_rows)
{
    cur = (unsigned)-1;
}

RtlLinkedDatasetCursor::RtlLinkedDatasetCursor()
{
    numRows = 0;
    rows = NULL;
    cur = (unsigned)-1;
}

void RtlLinkedDatasetCursor::init(unsigned _numRows, byte * * _rows)
{
    numRows = _numRows;
    rows = _rows;
    cur = (unsigned)-1;
}

const byte * RtlLinkedDatasetCursor::first()
{
    cur = 0;
    return cur < numRows ? rows[cur] : NULL;
}

const byte * RtlLinkedDatasetCursor::get()
{
    return cur < numRows ? rows[cur] : NULL;
}

bool RtlLinkedDatasetCursor::isValid()
{
    return (cur < numRows);
}

const byte * RtlLinkedDatasetCursor::next()
{
    if (cur < numRows)
        cur++;
    return cur < numRows ? rows[cur] : NULL;
}

const byte * RtlLinkedDatasetCursor::select(unsigned idx)
{
    cur = idx;
    return cur < numRows ? rows[cur] : NULL;
}


//---------------------------------------------------------------------------

bool rtlCheckInList(const void * lhs, IRtlDatasetCursor * cursor, ICompare * compare)
{
    const byte * cur;
    for (cur = cursor->first(); cur; cur = cursor->next())
    {
        if (compare->docompare(lhs, cur) == 0)
            return true;
    }
    return false;
}


void rtlSetToSetX(bool & outIsAll, size32_t & outLen, void * & outData, bool inIsAll, size32_t inLen, void * inData)
{
    outIsAll = inIsAll;
    outLen = inLen;
    outData = malloc(inLen);
    memcpy(outData, inData, inLen);
}


void rtlAppendSetX(bool & outIsAll, size32_t & outLen, void * & outData, bool leftIsAll, size32_t leftLen, void * leftData, bool rightIsAll, size32_t rightLen, void * rightData)
{
    outIsAll = leftIsAll | rightIsAll;
    if (outIsAll)
    {
        outLen = 0;
        outData = NULL;
    }
    else
    {
        outLen = leftLen+rightLen;
        outData = malloc(outLen);
        memcpy(outData, leftData, leftLen);
        memcpy((byte*)outData+leftLen, rightData, rightLen);
    }
}

//------------------------------------------------------------------------------

RtlCompoundIterator::RtlCompoundIterator()
{
    ok = false;
    numLevels = 0;
    iters = NULL;
    cursors = NULL;
}


RtlCompoundIterator::~RtlCompoundIterator()
{
    delete [] iters;
    delete [] cursors;
}


void RtlCompoundIterator::addIter(unsigned idx, IRtlDatasetSimpleCursor * iter, byte * * cursor)
{
    assertex(idx < numLevels);
    iters[idx] = iter;
    cursors[idx] = cursor;
}

void RtlCompoundIterator::init(unsigned _numLevels)
{
    numLevels = _numLevels;
    iters = new IRtlDatasetSimpleCursor * [numLevels];
    cursors = new byte * * [numLevels];
}

//Could either duplicate this function, N times, or have it as a helper function that accesses pre-defined virtuals.
bool RtlCompoundIterator::first(unsigned level)
{
    IRtlDatasetSimpleCursor * curIter = iters[level];
    if (level == 0)
    {
        const byte * cur = curIter->first();
        setCursor(level, cur);
        return (cur != NULL);
    }

    if (!first(level-1)) 
        return false;

    loop
    {
        const byte * cur = curIter->first();
        if (cur)
        {
            setCursor(level, cur);
            return true;
        }
        if (!next(level-1))
            return false;
    }
}

bool RtlCompoundIterator::next(unsigned level)
{
    IRtlDatasetSimpleCursor * curIter = iters[level];
    const byte * cur = curIter->next();
    if (cur)
    {
        setCursor(level, cur);
        return true;
    }

    if (level == 0)
        return false;

    loop
    {
        if (!next(level-1)) 
            return false;

        const byte * cur = curIter->first();
        if (cur)
        {
            setCursor(level, cur);
            return true;
        }
    }
}

//------------------------------------------------------------------------------

void RtlSimpleIterator::addIter(unsigned idx, IRtlDatasetSimpleCursor * _iter, byte * * _cursor)
{
    assertex(idx == 0);
    iter = _iter;
    cursor = _cursor;
    *cursor = NULL;
}

bool RtlSimpleIterator::first()
{
    const byte * cur = iter->first();
    *cursor = (byte *)cur;
    return (cur != NULL);
}

bool RtlSimpleIterator::next()
{
    const byte * cur = iter->next();
    *cursor = (byte *)cur;
    return (cur != NULL);
}

