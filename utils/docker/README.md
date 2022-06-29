## Docker container for building terragraph-image

This image is a [CROPS poky base](https://github.com/crops/poky-container)
on Ubuntu 18.04 and adds the xtensa esp32 toolchain used for 
[esp-idf v3.2](https://docs.espressif.com/projects/esp-idf/en/v3.2/get-started/linux-setup.html).

### Usage

Build the docker image in your local repository:

`docker image build -t tg-build .`

It is best to tag the image with a name (e.g. tg-build) so that it's easy
to reference and can be updated when necessary.

Start the container from your image and specify a workdir containing your
terragraph repo checkout, e.g.:

`docker run --rm -it -v ~:/workdir tg-build --workdir=/workdir`

This will start a docker container from the tg-build image we created and
mount your home directory at /workdir inside the container. You can then go
to your terragraph checkout and run commands to build.
