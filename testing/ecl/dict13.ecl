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

squareRecord := RECORD
  unsigned number;
  unsigned square;
END;

squareRecord makeSquare(unsigned value, unsigned delta) := TRANSFORM
  SELF.number := value;
  SELF.square := value * value + delta;
END;

numSquares := 200;
squares := DATASET(numSquares, makeSquare((COUNTER-1) DIV 2,(COUNTER-1) % 2));

squareRoots := DICTIONARY(squares, { UNSIGNED value := square => UNSIGNED root := number; });

//Check duplicate entries supplied to a dictionary always pick the first item

values := TABLE(NOFOLD(squares(square = number * number)), { unsigned value := square });

p := TABLE(values, { value, squareRoots[value].root });
OUTPUT(p);
