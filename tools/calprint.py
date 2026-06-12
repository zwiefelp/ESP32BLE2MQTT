import sys
import locale
import calendar
from datetime import datetime

locale.setlocale(locale.LC_ALL,'de_DE')

year = datetime.now().year
month = datetime.now().month
thisday = datetime.now().day

def printline(line: list[str]):
    text: str = "<b>"
    i = 0
    for elem in line:
        i = i + 1
        text = text + elem + " "
        if i == 5:
            text = text + "</b>"
    print(text)

def printtitle() -> None:
    title: str = F"{calendar.month_name[month].upper()} {year}" #.center(21)
    print(f"<span fgcolor=\"red\"><b>{title}</b></span>")

def printcal() -> None:
    cal = calendar.TextCalendar()
    line: list[str] = ["  ","  ","  ","  ","  ","  ","  "]
    head: list[str] = ["Mo","Di","Mi","Do","Fr","Sa","So"]

    printline(head)
    for day in cal.itermonthdays(year, month):
        if day != 0:
            weekday = calendar.weekday(year, month, day)
            if day == thisday:
                line[weekday]=f"<span background=\"red\"><b>{str(day).rjust(2)}</b></span>"
            else:    
                line[weekday]=str(day).rjust(2)
            
            if weekday == 6:
                printline(line)
                line = ["  ","  ","  ","  ","  ","  ","  "]
    else:
        if weekday < 6:
            printline(line)

def main() -> None:
    args = len(sys.argv) - 1
    pos = 1
    while (args >= pos):
        arg = sys.argv[pos]
        if arg == "-t":
            printtitle()
        if arg == "-c":
            printcal()
        pos = pos + 1

if __name__ == "__main__":
    main()

