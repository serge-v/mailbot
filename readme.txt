mailbot
=======

Tool to purge imap folders by search criteria.

Usage
=====

mailbot -l       -- list config files
mailbot NAME     -- purge email using config from ~/.config/mailbot/ dir

Config file example
===================

[imap]
url = imaps://imap.gmail.com:993/INBOX
login = myemail@gmail.com
password = emails/gmail # entry from password manager storage

# filter entry format:
# filter = DAYS_BACK,IMAP_QUERY

[purge]
filter = 2,FROM "bugzilla-noreply@freebsd.org"
filter = 7,TO "freebsd-arm@freebsd.org"
filter = 7,TO "freebsd-wireless@freebsd.org"
