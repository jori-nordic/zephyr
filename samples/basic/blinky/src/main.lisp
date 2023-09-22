
(defun configure-radio ()
    (register :constlat #x1)
    (register :hfclkstart #x1)
    (register :radio-power #x0)
    (register :radio-power #x1)

    (register :radio-shorts #x3)
    (register :radio-txpower #x0)
    (register :radio-mode #x3)

    (defvar access-addr #x8E89BED6)
    (register :radio-base0 (ash access-addr 8))
    (register :radio-prefix0 (logand #xFF (ash access-addr -24)))

    (register :radio-pcnf0 (logior #x8 (ash 1 8)))
    (register :radio-pcnf1 (logior
                            #x8
                            (ash #x3 16)
                            (ash #x1 25)))

    (register :radio-crccnf (logior #x3 (ash #x1 8)))
    (register :radio-crcpoly #x65B)
    (register :radio-crcinit #x555555)

    (register :radio-datawhiteiv 37)
    (register :radio-frequency #x2))

(defvar packetlist
  (list
   #x42
   #x13

   #xc0
   #x01
   #x13
   #x37
   #x42
   #xc0

   #x02
   #x01
   #x06
   #x09
   #x09

   #x48
   #x45
   #x4C
   #x4C
   #x4F
   #x20
   #x20
   )
  )

(defvar endptr (write-packet 0 packetlist))
(defvar ptr (- endptr (length packetlist)))

(defun start-adv ()
  (loop
    (register :radio-packetptr ptr)
    (register :radio-disabled 0)
    (register :radio-txen 1)
    (delay 1))
  )

(configure-radio)
(start-adv)
