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

#include "jiface.hpp"
#include "jtime.hpp"

#include "thexception.hpp"
#include "thmfilemanager.hpp"


#include "eclhelper.hpp"
#include "deftype.hpp"

#include "thdiskwrite.ipp"

class CDiskWriteActivityMaster : public CWriteMasterBase
{
public:
    CDiskWriteActivityMaster(CMasterGraphElement *info) : CWriteMasterBase(info) {}
    void init()
    {
        CWriteMasterBase::init();

        IHThorDiskWriteArg *helper=(IHThorDiskWriteArg *)queryHelper();
        IOutputMetaData *irecsize = helper->queryDiskRecordSize()->querySerializedMeta();
        IPropertyTree &props = fileDesc->queryProperties();
        if (0 != (helper->getFlags() & TDXgrouped))
            props.setPropBool("@grouped", true);
        if (irecsize->isFixedSize()) // fixed size
        {
            size32_t rSz = irecsize->getMinRecordSize();
            if (0 != (helper->getFlags() & TDXgrouped))
                ++rSz;
            props.setPropInt("@recordSize", rSz);
        }
        props.setPropInt("@formatCrc", helper->getFormatCrc());
    }
};

//---------------------------------------------------------------------------

class CsvWriteActivityMaster : public CWriteMasterBase
{
public:
    CsvWriteActivityMaster(CMasterGraphElement *info) : CWriteMasterBase(info) {}
    void done()
    {
        IPropertyTree &props = fileDesc->queryProperties();
        props.setPropBool("@csv", true);
        props.setProp("@format", "utf8n");
        IHThorCsvWriteArg *helper=(IHThorCsvWriteArg *)queryHelper();
        ICsvParameters *csvParameters = helper->queryCsvParameters();
        StringBuffer separator;
        const char *s = csvParameters->querySeparator(0);
        while (*s)
        {
            if (',' == *s)
                separator.append("\\,");
            else
                separator.append(*s);
            ++s;
        }
        props.setProp("@csvSeparate", separator.str());
        props.setProp("@csvQuote", csvParameters->queryQuote(0));
        props.setProp("@csvTerminate", csvParameters->queryTerminator(0));
        props.setProp("@csvEscape", csvParameters->queryEscape(0));

        CWriteMasterBase::done(); // will publish
    }
};

CActivityBase *createDiskWriteActivityMaster(CMasterGraphElement *info)
{
    return new CDiskWriteActivityMaster(info);
}

CActivityBase *createCsvWriteActivityMaster(CMasterGraphElement *info)
{
    return new CsvWriteActivityMaster(info);
}

