from PIL import Image, ImageDraw, ImageFont
import os
from pic_fs import PicFS

FONT_DIR = "./fonts/"
CACHE_DIR = "./cache/"
MAX_WIDTH = 296
MAX_HEIGHT = 128

number = 1

os.makedirs(CACHE_DIR, exist_ok=True)

# Define the color palette (white, black, red)
palette = [
    255, 255, 255,  # 0 white
    0, 0, 0,        # 1 black
    255, 0, 0       # 2 red
]


def make_nickname(mac, nickname, isorga = False, special = None, candy = None):
    padnick = 0
    mac = mac.upper() # ensure filenames are consistent
    # Create a new paletted image with indexed colors
    image = Image.new('P', (296, 128))

    # Assign the color palette to the image
    image.putpalette(palette)

    # Initialize the drawing context
    draw = ImageDraw.Draw(image)

    text_nickname = nickname
    text_mac = ':'.join(mac[i:i+2] for i in range(0, len(mac), 2)).upper()

    # Define the fonts and sizes
    font_size = 80
    while True:
        if special is not None and special == 'agency':
            padnick = -15
            font1 = ImageFont.truetype(FONT_DIR + 'AgencyFBBold.ttf', size=font_size)
        else:
            font1 = ImageFont.truetype(FONT_DIR + 'LLPIXEL3.ttf', size=font_size)
        nickname_bbox = draw.textbbox((0, 0), text_nickname, font=font1)
        if(nickname_bbox[2] - nickname_bbox[0] < MAX_WIDTH-20):
            break
        font_size -= 1

    font2 = ImageFont.truetype(FONT_DIR + 'TerminusTTF-4.49.2.ttf', size=12)
    mac_bbox = draw.textbbox((0, 0), text_mac, font=font2)

    # Import Background
    im2 = Image.open('static-graphics/wamp_30percent_border.png')
    image.paste(im2, (0, -84))
        
    # specials
    if special is not None and special == 'idefix':
        im3 = Image.open('static-graphics/idefix.png')\
                .resize((100, 100), resample=Image.NEAREST)
        # im3.putpalette([
        #     255, 255, 255, 
        #     255, 255, 255, 
        #     0, 0, 0  ])
        # im3 = im3.convert("RGBA")
        
        image.paste(im3, (200, -2), im3)

    if special is not None and special == 'waffle':
        im3 = Image.open('static-graphics/waffle.png')\
                .resize((80, 80), resample=Image.NEAREST)
        # im3.putpalette([
        #     255, 255, 255, 
        #     255, 255, 255, 
        #     0, 0, 0  ])
        # im3 = im3.convert("RGBA")
        
        image.paste(im3, (200, 10), im3)

    if special is not None and special == 'hedgehog':
        padnick = 10
        im3 = Image.open('static-graphics/hedgehog.png')\
                .resize((70, 70), resample=Image.NEAREST)
        # im3.putpalette([
        #     255, 255, 255, 
        #     255, 255, 255, 
        #     0, 0, 0  ])
        # im3 = im3.convert("RGBA")
        
        image.paste(im3, (225, 15), im3)

     # Hello my name is
    if special is not None and special == 'hello':
        padnick = -20
        hello_text = "Hello! My name is:"
        hello_font = ImageFont.truetype(FONT_DIR + 'Breaking.ttf', size=25)
        hello_bbox = draw.textbbox((0, 0), hello_text, font=hello_font)
        hello_pos = ((image.width - (hello_bbox[2] - hello_bbox[0])) // 2, 2)
        draw.text(hello_pos, hello_text, fill=2, font=hello_font)

    # Calculate the text positions to center the lines horizontally
    #print(f"{text_nickname} {font_size} {nickname_bbox[2] - nickname_bbox[0]}")
    nickname_pos = ((image.width - (nickname_bbox[2] - nickname_bbox[0])) // 2, ((image.height - (font_size) - padnick - (nickname_bbox[3] - nickname_bbox[1])) // 2)) # 35
    mac_pos = ((image.width - (mac_bbox[2] - mac_bbox[0])) // 2, 112)

    # Hier könnte Ihre Werbung stehen
    ad_text = "Thanks for visiting! See you at WAMP2024!"
    ad_font = ImageFont.truetype(FONT_DIR + 'Roboto-Medium.ttf', size=14)
    ad_bbox = draw.textbbox((0, 0), ad_text, font=ad_font)
    ad_pos = ((image.width - (ad_bbox[2] - ad_bbox[0])) // 2, 105)
    draw.rectangle((0,98,296,128),2)
    draw.text(ad_pos, ad_text, fill=0, font=ad_font)

    # Candy Icon
    # if candy is not None:
    #     candy_font = ImageFont.truetype(FONT_DIR + 'Candy.otf', size=50)
    #     draw.text((2, -5), candy, fill=1, font=candy_font)


    # Write the Nickname
    #draw.rectangle((nickname_pos, nickname_pos[0]+nickname_bbox[2], nickname_pos[1]+nickname_bbox[3]), 2)
    if isorga:
        draw.multiline_text((nickname_pos[0]-1,nickname_pos[1]-1), text_nickname, fill=1, font=font1)
        draw.multiline_text((nickname_pos[0]+1,nickname_pos[1]-1), text_nickname, fill=1, font=font1)
        draw.multiline_text((nickname_pos[0]-1,nickname_pos[1]+1), text_nickname, fill=1, font=font1)
        draw.multiline_text((nickname_pos[0]+1,nickname_pos[1]+1), text_nickname, fill=1, font=font1)

    draw.multiline_text(nickname_pos, text_nickname, fill=(2 if isorga else 1), font=font1)

    # Write the MAC
    #draw.rectangle((mac_pos[0]-1, mac_pos[1]-1, mac_pos[0]+mac_bbox[2]+1, mac_pos[1]+mac_bbox[3]+1), 0)
    #draw.text(mac_pos, text_mac, fill=1, font=font2) 
    

    finalize(image, mac)

def make_demo_image(mac):

    # Create a new paletted image with indexed colors
    image = Image.new('P', (296, 128))

    # Assign the color palette to the image
    image.putpalette(palette)

    # Initialize the drawing context
    draw = ImageDraw.Draw(image)

    # Define the text lines
    line1 = 'Herzlich willkommen zum'
    line2 = 'WAMP'
    # string to MAC colons
    line3 = 'MAC: '+':'.join(mac[i:i+2] for i in range(0, len(mac), 2)).upper()

    # Define the fonts and sizes
    font_line1 = ImageFont.truetype(FONT_DIR + 'Roboto-Regular.ttf', size=20)
    font_line2 = ImageFont.truetype(FONT_DIR + 'Roboto-Bold.ttf', size=80)
    font_line3 = ImageFont.truetype(FONT_DIR + 'TerminusTTF-4.49.2.ttf', size=12)

    # Calculate the text bounding boxes to get the text widths and heights
    text_bbox_line1 = draw.textbbox((0, 0), line1, font=font_line1)
    text_bbox_line2 = draw.textbbox((0, 0), line2, font=font_line2)
    text_bbox_line3 = draw.textbbox((0, 0), line3, font=font_line3)

    # Calculate the text positions to center the lines horizontally
    text_position_line1 = ((image.width - (text_bbox_line1[2] - text_bbox_line1[0])) // 2, 20)
    text_position_line2 = ((image.width - (text_bbox_line2[2] - text_bbox_line2[0])) // 2, 20)
    text_position_line3 = ((image.width - (text_bbox_line3[2] - text_bbox_line3[0])) // 2, 110)

    # Import BMP
    im2 = Image.open('static-graphics/wamp_30percent_border.png')
    image.paste(im2, (0, -84))

    # Write the text on the image
    # draw.text(text_position_line1, line1, fill=2, font=font_line1)  # Use palette index 1 for red color
    draw.text(text_position_line2, line2, fill=1, font=font_line2)  # Use palette index 2 for black color
    draw.text(text_position_line3, line3, fill=1, font=font_line3)  # Use palette index 1 for black color


    # Write the text on the image
    draw.rectangle((text_position_line3[0]-1, text_position_line3[1]-1, text_position_line3[0]+text_bbox_line3[2]+1, text_position_line3[1]+text_bbox_line3[3]+1), 0)
    draw.text(text_position_line3, line3, fill=1, font=font_line3) 
    
    # Import BMP
    # im2 = Image.open('static-graphics/alien.bmp')
    # image.paste(im2, (50, 55))



    finalize(image, mac)

def make_ap_image(mac):

    # Create a new paletted image with indexed colors
    image = Image.new('P', (296, 128))

    # Assign the color palette to the image
    image.putpalette(palette)

    # Initialize the drawing context
    draw = ImageDraw.Draw(image)

    # Define the text lines
    line1 = 'OpenEPaperLink-PyStation '
    line2 = ':'.join(mac[i:i+2] for i in range(0, len(mac), 2)).upper()

    # Define the fonts and sizes
    font_line1 = ImageFont.truetype(FONT_DIR + 'Roboto-Bold.ttf', size=20)
    font_line2 = ImageFont.truetype(FONT_DIR + 'TerminusTTF-4.49.2.ttf', size=20)

    # Calculate the text bounding boxes to get the text widths and heights
    text_bbox_line1 = draw.textbbox((0, 0), line1, font=font_line1)
    text_bbox_line2 = draw.textbbox((0, 0), line2, font=font_line2)

    # Calculate the text positions to center the lines horizontally
    text_position_line1 = ((image.width - (text_bbox_line1[2] - text_bbox_line1[0])) // 2, 45)
    text_position_line2 = ((image.width - (text_bbox_line2[2] - text_bbox_line2[0])) // 2, 75)

    # Write the text on the image
    draw.text(text_position_line1, line1, fill=1, font=font_line1) 
    draw.text(text_position_line2, line2, fill=1, font=font_line2)  

    finalize(image, mac)

def make_test_screen(mac):
    # Create a new paletted image with indexed colors
    image = Image.new('P', (296, 128))

    # Assign the color palette to the image
    image.putpalette(palette)

    # Initialize the drawing context
    draw = ImageDraw.Draw(image)

    # string to MAC colons
    line3 = 'MAC: '+':'.join(mac[i:i+2] for i in range(0, len(mac), 2)).upper()

    # Define the fonts and sizes
    font_line3 = ImageFont.truetype(FONT_DIR + 'TerminusTTF-4.49.2.ttf', size=12)

    # Calculate the text bounding boxes to get the text widths and heights
    text_bbox_line3 = draw.textbbox((0, 0), line3, font=font_line3)

    # Calculate the text positions to center the lines horizontally
    text_position_line3 = ((image.width - (text_bbox_line3[2] - text_bbox_line3[0])) // 2, 110)

   
    # Write the text on the image
    draw.text(text_position_line3, line3, fill=1, font=font_line3)  # Use palette index 1 for black color

    draw.rectangle((0, 0, image.width, image.height), 2)
    
    # Write the text on the image
    draw.rectangle((text_position_line3[0]-1, text_position_line3[1]-1, text_position_line3[0]+text_bbox_line3[2]+1, text_position_line3[1]+text_bbox_line3[3]+1), 0)
    draw.text(text_position_line3, line3, fill=1, font=font_line3) 

    finalize(image, mac)

def make_number(mac):
    global number
    # Create a new paletted image with indexed colors
    image = Image.new('P', (296, 128))

    # Assign the color palette to the image
    image.putpalette(palette)

    # Initialize the drawing context
    draw = ImageDraw.Draw(image)

    # Define the text lines
    line2 = number.__str__()
    number += 1
    # string to MAC colons
    line3 = 'MAC: '+':'.join(mac[i:i+2] for i in range(0, len(mac), 2)).upper()

    # Define the fonts and sizes
    font_line2 = ImageFont.truetype(FONT_DIR + 'Roboto-Bold.ttf', size=80)
    font_line3 = ImageFont.truetype(FONT_DIR + 'TerminusTTF-4.49.2.ttf', size=12)

    # Calculate the text bounding boxes to get the text widths and heights
    text_bbox_line2 = draw.textbbox((0, 0), line2, font=font_line2)
    text_bbox_line3 = draw.textbbox((0, 0), line3, font=font_line3)

    # Calculate the text positions to center the lines horizontally
    text_position_line2 = ((image.width - (text_bbox_line2[2] - text_bbox_line2[0])) // 2, 20)
    text_position_line3 = ((image.width - (text_bbox_line3[2] - text_bbox_line3[0])) // 2, 110)

    # Write the text on the image
    # draw.text(text_position_line1, line1, fill=2, font=font_line1)  # Use palette index 1 for red color
    draw.text(text_position_line2, line2, fill=1, font=font_line2)  # Use palette index 2 for black color
    draw.text(text_position_line3, line3, fill=1, font=font_line3)  # Use palette index 1 for black color

    # Write the text on the image
    draw.rectangle((text_position_line3[0]-1, text_position_line3[1]-1, text_position_line3[0]+text_bbox_line3[2]+1, text_position_line3[1]+text_bbox_line3[3]+1), 0)
    draw.text(text_position_line3, line3, fill=1, font=font_line3) 

    finalize(image, mac)

def make_big_image(mac, index):
     # Load the image
    image = Image.open("static-graphics/wamp_big.png")

    # Define the slice dimensions
    slice_width = 296
    slice_height = 128

    # Calculate the number of slices horizontally and vertically
    num_slices_horizontal = image.width // slice_width
    num_slices_vertical = image.height // slice_height

    # Iterate through each slice and save them
    sliceNo = 1
    for i in range(num_slices_vertical):
        for j in range(num_slices_horizontal):
            # Calculate the slice coordinates
            left = j * slice_width
            upper = i * slice_height
            right = left + slice_width
            lower = upper + slice_height

            if sliceNo == index:
                # Crop the slice
                slice = image.crop((left, upper, right, lower))
                print("Slice at Position {left}, {upper}, {right}, {lower}".format(left=left, upper=upper, right=right, lower=lower))
                print("Slice {index}: {i}/{j}".format(index=index, i=i, j=j))
                
                # Save the slice with a unique filename
                finalize(slice, mac)
            
            sliceNo += 1
                


def finalize(image, mac):
    # Convert the image to 24-bit RGB
    rgb_image = image.convert('RGB')

    # Save the image as JPEG with maximum quality
    image_path = CACHE_DIR + mac + '.png'
    rgb_image.save(image_path, 'PNG')

    picfs = PicFS()
    data = picfs.load_image(mac + '.png', 0x21)
    #print(data.hex())
    # write to file
    with open(CACHE_DIR + mac + '.raw', 'wb') as f:
        f.write(data)

    print("Image created for MAC: " + mac)




if __name__ == '__main__':

    make_nickname("00000218d7c63b12", "foorschtbar", True, None, "V")
    make_demo_image("0000021f982f3b19")
    make_demo_image("00000218cc363b17")
    make_demo_image("0000021f48a03b13")
    
    # [...]
