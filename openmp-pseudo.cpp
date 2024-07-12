//Pseudo code of threaded H.264 encoder using taskqueing with OpenMP

#pragma intel omp parallel taskqueing
{
    while (there is frame to encode) {
        #pragma omp critical
        {
            if (there is no free entry in image buffer) {
                commit the encoded frame;
                release the entry;
                load the original picture to memory;
                prepare for encoding;
            }
        }
        for (all slices in frame) {
            #pragma intel omp task
            {
                collect statistics for one slice;
            }
        }
    }
}

#pragma omp barrier
#pragma intel omp parallel taskqueing
{
    while (there is frame to encode) {
        #pragma omp critical
        {
            if (there is no free entry in image buffer) {
                commit the encoded frame;
                release the entry;
                load the original picture to memory;
                prepare for encoding;
            }
        }
        for (all slices in frame) {
            #pragma intel omp task
            {
                encode one slice using collected statistics;
            }
        }
    }
}