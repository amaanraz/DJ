

import array
import wave
import struct

left_list = []
right_list = []

def read_wav_file(wav_file):
    with wave.open(wav_file, 'rb') as wav:
        num_channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        frame_rate = wav.getframerate()

        print("Number of channels:", num_channels)
        print("Sample width (bytes):", sample_width)
        print("Frame rate (samples per second):", frame_rate)

        # Read and output each sample value
        print("Sample values:")
        for j in range(wav.getnframes()):
            # Read one frame (one sample per channel)
            frame = wav.readframes(1)
            # if(j<10000):
            #   print(frame)
            if sample_width == 1:
                # If 8-bit samples, convert to integers (-128 to 127)
                sample_values = [int.from_bytes(frame, 'signed') for _ in range(num_channels)]
            elif sample_width == 2:
                # If 16-bit samples, interpret as little-endian signed integers
                sample_values = [int.from_bytes(frame[i:i+2], 'little', signed=True) for i in range(0, len(frame), 2)]
            else:
                raise ValueError("Unsupported sample width")
            left_list.append(sample_values[0])
            right_list.append(sample_values[1])
            # print(sample_values)  # Output the sample values

# Example usage
wav_file = 'bassdrop.wav'  # Replace with your WAV file path
read_wav_file(wav_file)


print("len is ",len(left_list))
left_file_path = 'left_list_bassdrop.data'
right_file_path = 'right_list_bassdrop.data'

# Writing the lists to binary files
with open(left_file_path, 'wb') as left_file:
    for num in left_list:
        # Pack the integer as 4 bytes (using struct to handle it)
        left_file.write(struct.pack('i', num))

with open(right_file_path, 'wb') as right_file:
    for num in right_list:
        # Pack the integer as 4 bytes
        right_file.write(struct.pack('i', num))

print(left_list[0])
print(right_list[0])
# loop through array j
# loops for delay
# caculate index 
    # divide = 960000/44100
    # index / divide
# play audio at index