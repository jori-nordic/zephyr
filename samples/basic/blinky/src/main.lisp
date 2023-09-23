;; redefine fns to be able to run this in CL
(defun register (addr value)
  (format t "reg write: ~x: ~x" addr value))

(defun write-packet (ptr array)
  (format t "~A" array)
  ;; dummy ptr is returned
  10)

(defun delay (ms) (sleep (/ ms 1000)))
;; end redef

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
                            #xFF
                            (ash #x3 16)
                            (ash #x1 25)))

    (register :radio-crccnf (logior #x3 (ash #x1 8)))
    (register :radio-crcpoly #x65B)
    (register :radio-crcinit #x555555)

    (register :radio-datawhiteiv 37)
    (register :radio-frequency #x2))

(defun make-ad (type payload)
  (append '()
          (list (+ 1 (length payload)))
          (list type)
          payload))

(defun ascii (string)
  "Converts a string to a list of ASCII values"
  (let ((al '()))
    (dotimes (i (length string))
      (push (char-code (char string i)) al))
    (reverse al)))

(defvar empty-pdu
  (list
   #x42 #x13

   #xc0 #x01 #x13 #x37 #x42 #xc0
   ))

(defvar pdu
  (append empty-pdu
          (make-ad #x1 '(#x6))
          (make-ad #x9 (ascii "ulisp   "))))

(defun start-adv (pdu)
  (let ((ptr
          (- (write-packet 0 pdu)
             (- (length pdu) 1))))

    (format t "ptr: ~X" ptr)
    (loop
      (register :radio-packetptr ptr)
      (register :radio-disabled 0)
      (register :radio-txen 1)
      (delay 20))))

(configure-radio)
(start-adv pdu)
