# Clang Semantic Import Tool [![Sonarcloud Status](https://sonarcloud.io/api/project_badges/measure?project=CLANG_SEMANTIC_IMPORT&metric=alert_status)](https://sonarcloud.io/dashboard?id=CLANG_SEMANTIC_IMPORT)

clang-semantic-import (CSI) is a simple tool for sorting the  import/include (```#include``` / ```#import```) and semantic import where we can.
This tool was build using libtooling[1]. with clang 7 in mind (since Mojave was ship with that version).

## Available options

The primary function of CSI is **sort the imports**, but we have more option to perform in the code, like:

1. ```-move-import-order```: Sort (move) the semantic imports after the old way of importing (alias ```-mio```)
1. ```-remove-import=NAME_OF_THE_IMPORT```: Remove desired imports (alias ```-ri=NAME_OF_THE_IMPORT```)
2. ```-order```: Order imports alfabetically (alias ```-o```)
3. ```-rewrite```: Save all the actions performed in the Source Code (alias ```-r```)

## How to compile

To build this tool, i download the specific llvm version from llvm.org. If you want to contribute please go to the **contribute** section.

To compile the tool i made a little Dockerfile that download clang 7 and make everthing ready to compile. The step to deploy the docker container is:


1. ``` docker build -t clang . ``` 

2. ``` docker run -it -v $PWD:/home clang ```

After you are in the container, you are ready to build the tool:


``` make ```

## How to use it

TO BE DONE

## Improvements 

TO BE DONE