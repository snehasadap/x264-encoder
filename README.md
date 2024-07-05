# video-encoder
## Instructions
1. Install FFmpeg
```sh
  brew install ffmpeg
```
If you don't have Homebrew installed, view [this](https://docs.brew.sh/Installation) page for setup instructions.
2. Create a build directory within your project (assuming your project is named `video-encoder`)
```sh
  mkdir -p video-encoder/build
  cd video-encoder/build
```
3. Compile `main.cpp`
```sh
  cmake ..
  make
```
4. Run `main.cpp`
```sh
  ./video_encoder your-video-file.mov
```
Please specify the directory of your `.mov` file, it should look something like this: `\users\your-username\file-directory\your-video-file.mov`.

## Resources
https://trac.ffmpeg.org/wiki/Encode/H.264
