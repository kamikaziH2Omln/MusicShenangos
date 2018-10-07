from tkinter import *

from tkinter import messagebox


top = Tk()
top.geometry("400x400")


# Version Info
verInfo= "0.0.1"
verStatement= ("This is Version {}. I still don't know what I am doing".format(verInfo))


# Introduction as Label
intro = Label(top, text= "Welcome to the Portable Audio Scaling Program Version {}".format(verInfo))
intro.pack()


# Version Info Messagebox
def VerInfo():
    messagebox.showinfo("Version Info", verStatement)


# Radio Button Selector and Label
def selector():
    oSelection = ("You Selected {} as the Desired Output Format".format(str(var.get())))
    label.config(text = oSelection)


var = StringVar()
radio128 = Radiobutton(top, text= "CBR 128 Kbps", variable=var, value= "CBR 128", command= selector)
radio128.pack(anchor=W)
radio256 = Radiobutton(top, text= "CBR 256 Kbps", variable=var, value= "CBR 256", command= selector)
radio256.pack(anchor=W)
radio320 = Radiobutton(top, text= "CBR 320 Kbps", variable=var, value= "CBR 320", command= selector)
radio320.pack(anchor=W)

label = Label(top)
label.pack()

# Menubox which is currently useless
"""
def menu():
    fileWin = Toplevel(top)
    button = Button(fileWin, text="Do Nothing Button")
    button.pack()
"""



# Menubar
menubar = Menu(top)
filemenu = Menu(menubar, tearoff=0)
filemenu.add_command(label="Version Info", command=VerInfo)


# Add Separator in File Column
filemenu.add_separator()

# Exit Command for Program
filemenu.add_command(label="Exit", command=top.quit)
menubar.add_cascade(label="File", menu= filemenu)


# Unnecessary Textbox b/c user input could be used.
# Replaced with Label above.
"""
IntroText= Text(top)
IntroText.insert(INSERT, "Welcome to the Portable Audio Scaling Program Version {}".format(verInfo))
IntroText.insert(END, "")
IntroText.place(x=0, y=0)
IntroText.pack()
"""

# Button for Version Info
B1 = Button(top, text = "Version Info", command = VerInfo, activebackground= "cyan")
B1.pack(anchor=W)
# B1.place(x = 0,y = 200) #

top.config(menu=menubar)
top.mainloop()