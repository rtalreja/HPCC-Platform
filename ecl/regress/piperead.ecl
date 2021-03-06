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

namesRecord :=
        RECORD
string10            forename;
string10            surname;
string2             nl := '\r\n';
        END;

d := PIPE('pipeRead 200', namesRecord);
output(d,,'names.d00');
//output(d,{TRIM(forename),TRIM(surname)},'varnames.d00');

