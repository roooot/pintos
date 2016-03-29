# Pintos

Operating Systems and System Programming for Electrical Engineering (EE415) by Jinhwan Lee, [KAIST](http://www.kaist.ac.kr), Spring 2016.


## Environments

- Use docker on [pintos-kaist-ee415](https://root.plus:10000/jhlee/dockerfile-pintos) (Recommended)
    - Attach volume `/pintos` with your pintos directory, and commands.
        ``` sh
        docker run -i -t -v <PATH_TO_PINTOS>:/pintos pintos-kaist-ee415 bash
        ```

    - If you want to run Docker as a non-root user `<USER>`, put `entry.sh` to `pintos`:

        ``` sh
        #!/bin/bash
        useradd -u <UID> <USER>
        su <USER> --session-command bash
        ```

        and then run  command.
    
        ``` sh
        docker run -i -t -v <PATH_TO_PINTOS>:/pintos pintos-kaist-ee415 /pintos/entry.sh
        ```

- Linux
    - Install X development libraries
    - Install development tools
        - Including gcc, make, perl, gdb (option) and so on...
- Bochs 2.2.6
    - Get the source from <http://ina.kaist.ac.kr/ee415/S16/bochs-2.2.6.tar.gz>.
    - Must use patches in pintos
    - Use script (pintos/src/misc/bochs-2.2.6-build.sh)

        ``` sh
        $ ./bochs-2.2.6-build.sh
        usage: env SRCDIR=<srcdir> PINTOSDIR=<srcdir> DSTDIR=<dstdir> sh ./bochs-2.2.6-build.sh
        where <srcdir> contains bochs-2.2.6.tar.gz
        and <pintosdir> is the root of the pintos source tree
        and <dstdir> is the installation prefix (e.g. /usr/local)
        $ env SRCDIR=<PATH_TO_BOCHS>/bochs-2.2.6/ PINTOSDIR=<PATH_TO_PINTOS> DSTDIR=/usr/local/ sh <PATH_TO_PINTOS>/src/bochs-2.2.6-build.sh
        ```
    - Maybe need the latest version of Binutils & Make

## Install Pintos

- Get the source from <http://ina.kaist.ac.kr/ee415/S16/pintos.tar.gz>.
- Extract the source for Pintos into `pintos/src`:

    ``` sh
    tar xzf pintos.tar.gz
    ```
- Test your installation

    ``` sh
    cd pintos/src/threads
    make
    ../utils/pintos run alarm-multiple
    ```

## Running Pintos

- Add `pintos/src/utils` folder to `$PATH`
- Execute `pintos [option] -- [argument]`
    - Option
        - Configuring the simulator or the virtual hardware
    - Argument
        - Each argument is passed to Pintos kernel verbatim
        - `pintos run alarm-multiple` instructs the kernel to run alarm-multiple
        - `pintos -v -- -q run alarm-multiple` clears bochs after execution
    - Pintos script
        - Parse command line
        - Find disks
        - Prepare arguments
        - Run VM (bochs)

## Testing Pintos

### Testing

- `make check` (in build directory)
- `make grade`

### Tests directory

`pintos/src/threads/build/tests`

### Graded result

`pintos/src/threads/build/grade`