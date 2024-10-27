# x264-encoder
The purpose of this encoder is to acheive a target file size of 10 MB with two-pass encoding. Renders in 60 fps.
## Instructions
1. Install FFmpeg

    ```sh
    brew install ffmpeg
    ```
    If you don't have Homebrew installed, view [this](https://docs.brew.sh/Installation) page for setup instructions.

2. Create your build directory (assuming your project is named `video-encoder`)
   ```sh
   mkdir -p video-encoder/build
   cd video-encoder/build
   ```

3. Run CMakeLists.txt
    ```sh
    cmake ..
    make
    ```
    From then on, you can just run `make` to compile the encoder.
   
5. Run `main.cpp`
   ```sh
   ./VideoEncoder your-video-file.mov
   ```
  
    Please specify the directory of your `.mov` file, it should look something like this: `\Users\your-username\directory\your-video-file.mov`.

## Resources
https://trac.ffmpeg.org/wiki/Encode/H.264
