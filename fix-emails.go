package main

import (
	"os"
	"bufio"
	"fmt"
	"strings"
)

func main() {
	f, err := os.Open("1.txt")
	if err != nil {
		panic(err)
	}

	linereader := bufio.NewScanner(f)
	
	for linereader.Scan() {
		fname := linereader.Text()
//		println(fname)
		
		fmail, err := os.Open(fname)
		if err != nil {
			panic(err)
		}
		defer fmail.Close()
	
		mscanner := bufio.NewScanner(fmail)
		block := ""
		is_block := false

		for mscanner.Scan() {
			line := mscanner.Text()

			idx := strings.Index(line, "A charge ")
			if idx < 0 {
				idx = strings.Index(line, "a gas station charge")
			}

			if idx >= 0 {
				line = line[idx:]
				is_block = true
			}
			
			line = strings.Trim(line, ">")

			idx = strings.Index(line, "Do not reply")
			if idx >= 0 {
					block += line[:idx]
					block = strings.Replace(block, "=", "", 1)
//					fmt.Println("======================")
//					fmt.Println(fname)
//					fmt.Println("----------------------")
//					fmt.Println(block)
					block = strings.Trim(block, "\r\n ")
					if len(block) > 0 {
						fmail_edited, err := os.Create(fname + ".edited")
						if err != nil {
							panic(err)
						}
						defer fmail_edited.Close()
						fmt.Fprintln(fmail_edited, block)
						fmt.Println(fname)
					}
					break
			}

			if is_block {
				block += line
			}
		}
	}
}
