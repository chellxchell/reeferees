# reeferees

## Set Up Notes
* [Download git](https://git-scm.com/downloads)
* Open up command prompt and type in `cd Documents`
* In the command prompt, type in `git clone https://github.com/chellxchell/reeferees.git`
* In File Explorer, navigate to Documents > reeferees > saveToSD and open up 'saveToSD'
* You'll need to download 3 libraries: RTClib.h, LowPower.h, and SdFat.h. Go to each of these three links, click the green "Code" button with a dropdown, and click "Download Zip"
  * https://github.com/MrAlvin/RTClib
  * https://github.com/rocketscream/Low-Power
  * https://github.com/greiman/SdFat
* Once you have each zip file, unzip it. This can be done by right-clicking the zip file in the Downloads folder and clicking "Extract All"
* Move all three unzipped folders into Documents > Arduino > Libraries
* Make sure the data logger is connected properly (see Debugging notes below)
* Go back to the saveToSd file and press "upload" to upload the script to the Arduino
* Go to Tools > Serial Monitor to view the console output
* If you change any code and you want to push it to github, open up Command Prompt and do the following:
  * `cd Documents/reeferees`
  * `git add .`
  * `git commit -m "some message"`
  * `git push`
## Debugging notes
* Port is Com 5
* Order is brown-left, blue-right with the ProMini facing closest to you
* Set to 115200 baud
* Download library zip files directly
