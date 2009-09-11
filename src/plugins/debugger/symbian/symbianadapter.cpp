/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "symbianadapter.h"
#ifndef STANDALONE_RUNNER
#include "gdb/gdbengine.h"
#endif

#define TrkCB(s) TrkCallback(this, &SymbianAdapter::s)


using namespace trk;

enum { KnownRegisters = RegisterPSGdb + 1};

static const char *registerNames[KnownRegisters] =
{
    "A1", "A2", "A3", "A4",
    0, 0, 0, 0,
    0, 0, 0, "AP",
    "IP", "SP", "LR", "PC",
    "PSTrk", 0, 0, 0,
    0, 0, 0, 0,
    0, "PSGdb"
};

static QByteArray dumpRegister(int n, uint value)
{
    QByteArray ba;
    ba += ' ';
    if (n < KnownRegisters && registerNames[n]) {
        ba += registerNames[n];
    } else {
        ba += '#';
        ba += QByteArray::number(n);
    }
    ba += "=" + hexxNumber(value);
    return ba;
}

namespace Debugger {
namespace Internal {

trk::Endianness m_registerEndianness = LittleEndian;

SymbianAdapter::SymbianAdapter()
{
    m_running = false;
    m_gdbAckMode = true;
    m_verbose = 2;
    m_serialFrame = false;
    m_bufferedMemoryRead = true;
    m_rfcommDevice = "/dev/rfcomm0";

    uid_t userId = getuid();
    m_gdbServerName = QString("127.0.0.1:%1").arg(2222 + userId);

    connect(&m_gdbProc, SIGNAL(readyReadStandardError()),
        this, SIGNAL(readyReadStandardError()));
    connect(&m_gdbProc, SIGNAL(readyReadStandardOutput()),
        this, SIGNAL(readyReadStandardOutput()));
    connect(&m_gdbProc, SIGNAL(error(QProcess::ProcessError)),
        this, SLOT(handleGdbError(QProcess::ProcessError)));
    connect(&m_gdbProc, SIGNAL(finished(int, QProcess::ExitStatus)),
        this, SLOT(handleGdbFinished(int, QProcess::ExitStatus)));
    connect(&m_gdbProc, SIGNAL(started()),
        this, SLOT(handleGdbStarted()));
    connect(&m_gdbProc, SIGNAL(stateChanged(QProcess::ProcessState)),
        this, SLOT(handleGdbStateChanged(QProcess::ProcessState)));

    connect(&m_rfcommProc, SIGNAL(readyReadStandardError()),
        this, SLOT(handleRfcommReadyReadStandardError()));
    connect(&m_rfcommProc, SIGNAL(readyReadStandardOutput()),
        this, SLOT(handleRfcommReadyReadStandardOutput()));
    connect(&m_gdbProc, SIGNAL(error(QProcess::ProcessError)),
        this, SLOT(handleRfcommError(QProcess::ProcessError)));
    connect(&m_gdbProc, SIGNAL(finished(int, QProcess::ExitStatus)),
        this, SLOT(handleRfcommFinished(int, QProcess::ExitStatus)));
    connect(&m_gdbProc, SIGNAL(started()),
        this, SLOT(handleRfcommStarted()));
    connect(&m_gdbProc, SIGNAL(stateChanged(QProcess::ProcessState)),
        this, SLOT(handleRfcommStateChanged(QProcess::ProcessState)));

    if (m_verbose > 1)
        m_trkDevice.setVerbose(true);
    m_trkDevice.setSerialFrame(m_serialFrame);

    connect(&m_trkDevice, SIGNAL(logMessage(QString)),
        this, SLOT(trkLogMessage(QString)));
}

SymbianAdapter::~SymbianAdapter()
{
    m_gdbServer.close();
    logMessage("Shutting down.\n");
}

void SymbianAdapter::trkLogMessage(const QString &msg)
{
    logMessage("TRK " + msg);
}

void SymbianAdapter::setGdbServerName(const QString &name)
{
    m_gdbServerName = name;
}

QString SymbianAdapter::gdbServerIP() const
{
    int pos = m_gdbServerName.indexOf(':');
    if (pos == -1)
        return m_gdbServerName;
    return m_gdbServerName.left(pos);
}

uint SymbianAdapter::gdbServerPort() const
{
    int pos = m_gdbServerName.indexOf(':');
    if (pos == -1)
        return 0;
    return m_gdbServerName.mid(pos + 1).toUInt();
}

QByteArray SymbianAdapter::trkContinueMessage()
{
    QByteArray ba;
    appendInt(&ba, m_session.pid);
    appendInt(&ba, m_session.tid);
    return ba;
}

QByteArray SymbianAdapter::trkReadRegisterMessage()
{
    QByteArray ba;
    appendByte(&ba, 0); // Register set, only 0 supported
    appendShort(&ba, 0);
    appendShort(&ba, RegisterCount - 1); // last register
    appendInt(&ba, m_session.pid);
    appendInt(&ba, m_session.tid);
    return ba;
}

QByteArray SymbianAdapter::trkReadMemoryMessage(uint addr, uint len)
{
    QByteArray ba;
    appendByte(&ba, 0x08); // Options, FIXME: why?
    appendShort(&ba, len);
    appendInt(&ba, addr);
    appendInt(&ba, m_session.pid);
    appendInt(&ba, m_session.tid);
    return ba;
}

void SymbianAdapter::startInferior()
{
    QString errorMessage;
    if (!m_trkDevice.open(m_rfcommDevice, &errorMessage)) {
        emit output("LOOPING");
        QTimer::singleShot(1000, this, SLOT(startInferior()));
        return;
    }

    m_trkDevice.sendTrkInitialPing();
    sendTrkMessage(0x01); // Connect
    sendTrkMessage(0x05, TrkCB(handleSupportMask));
    sendTrkMessage(0x06, TrkCB(handleCpuType));
    sendTrkMessage(0x04, TrkCB(handleTrkVersions)); // Versions
    //sendTrkMessage(0x09); // Unrecognized command
    //sendTrkMessage(0x4a, 0,
    //    "10 " + formatString("C:\\data\\usingdlls.sisx")); // Open File
    //sendTrkMessage(0x4B, 0, "00 00 00 01 73 1C 3A C8"); // Close File

    QByteArray ba;
    appendByte(&ba, 0); // ?
    appendByte(&ba, 0); // ?
    appendByte(&ba, 0); // ?

    QByteArray file("C:\\sys\\bin\\filebrowseapp.exe");
    appendString(&ba, file, TargetByteOrder);
    sendTrkMessage(0x40, TrkCB(handleCreateProcess), ba); // Create Item
    //sendTrkMessage(TRK_WRITE_QUEUE_NOOP_CODE, TrkCB(startGdbServer));
}

void SymbianAdapter::logMessage(const QString &msg)
{
    if (m_verbose)
        emit output(msg);
}

//
// Gdb
//
void SymbianAdapter::handleGdbConnection()
{
    logMessage("HANDLING GDB CONNECTION");

    m_gdbConnection = m_gdbServer.nextPendingConnection();
    connect(m_gdbConnection, SIGNAL(disconnected()),
            m_gdbConnection, SLOT(deleteLater()));
    connect(m_gdbConnection, SIGNAL(readyRead()),
            this, SLOT(readGdbServerCommand()));
}

static inline QString msgGdbPacket(const QString &p)
{
    return QLatin1String("gdb:                              ") + p;
}

void SymbianAdapter::readGdbServerCommand()
{
    QByteArray packet = m_gdbConnection->readAll();
    m_gdbReadBuffer.append(packet);

    logMessage("gdb: -> " + QString::fromAscii(packet));
    if (packet != m_gdbReadBuffer)
        logMessage("buffer: " + m_gdbReadBuffer);

    QByteArray &ba = m_gdbReadBuffer;
    while (ba.size()) {
        char code = ba.at(0);
        ba = ba.mid(1);

        if (code == '+') {
            //logMessage("ACK");
            continue;
        }

        if (code == '-') {
            logMessage("NAK: Retransmission requested");
            continue;
        }

        if (code == char(0x03)) {
            logMessage("INTERRUPT RECEIVED");
            interruptInferior();
            continue;
        }

        if (code != '$') {
            logMessage("Broken package (2) " + quoteUnprintableLatin1(ba)
                + hexNumber(code));
            continue;
        }

        int pos = ba.indexOf('#');
        if (pos == -1) {
            logMessage("Invalid checksum format in "
                + quoteUnprintableLatin1(ba));
            continue;
        }

        bool ok = false;
        uint checkSum = ba.mid(pos + 1, 2).toUInt(&ok, 16);
        if (!ok) {
            logMessage("Invalid checksum format 2 in "
                + quoteUnprintableLatin1(ba));
            return;
        }

        //logMessage(QString("Packet checksum: %1").arg(checkSum));
        byte sum = 0;
        for (int i = 0; i < pos; ++i)
            sum += ba.at(i);

        if (sum != checkSum) {
            logMessage(QString("ERROR: Packet checksum wrong: %1 %2 in "
                + quoteUnprintableLatin1(ba)).arg(checkSum).arg(sum));
        }

        QByteArray cmd = ba.left(pos);
        ba.remove(0, pos + 3);
        handleGdbServerCommand(cmd);
    }
}

bool SymbianAdapter::sendGdbServerPacket(const QByteArray &packet, bool doFlush)
{
    if (!m_gdbConnection) {
        logMessage(QString::fromLatin1("Cannot write to gdb: No connection (%1)")
            .arg(QString::fromLatin1(packet)));
        return false;
    }
    if (m_gdbConnection->state() != QAbstractSocket::ConnectedState) {
        logMessage(QString::fromLatin1("Cannot write to gdb: Not connected (%1)")
            .arg(QString::fromLatin1(packet)));
        return false;
    }
    if (m_gdbConnection->write(packet) == -1) {
        logMessage(QString::fromLatin1("Cannot write to gdb: %1 (%2)")
            .arg(m_gdbConnection->errorString()).arg(QString::fromLatin1(packet)));
        return false;
    }
    if (doFlush)
        m_gdbConnection->flush();
    return true;
}

void SymbianAdapter::sendGdbServerAck()
{
    if (!m_gdbAckMode)
        return;
    QByteArray packet = "+";
    logMessage("gdb: <- " + packet);
    sendGdbServerPacket(packet, false);
}

void SymbianAdapter::sendGdbServerMessage(const QByteArray &msg, const QByteArray &logNote)
{
    byte sum = 0;
    for (int i = 0; i != msg.size(); ++i)
        sum += msg.at(i);

    char checkSum[30];
    qsnprintf(checkSum, sizeof(checkSum) - 1, "%02x ", sum);

    //logMessage(QString("Packet checksum: %1").arg(sum));

    QByteArray packet;
    packet.append("$");
    packet.append(msg);
    packet.append('#');
    packet.append(checkSum);
    int pad = qMax(0, 24 - packet.size());
    logMessage("gdb: <- " + packet + QByteArray(pad, ' ') + logNote);
    sendGdbServerPacket(packet, true);
}

void SymbianAdapter::sendGdbServerMessageAfterTrkResponse(const QByteArray &msg,
    const QByteArray &logNote)
{
    QByteArray ba = msg + char(1) + logNote;
    sendTrkMessage(TRK_WRITE_QUEUE_NOOP_CODE, TrkCB(reportToGdb), "", ba); // Answer gdb
}

void SymbianAdapter::reportToGdb(const TrkResult &result)
{
    QByteArray message = result.cookie.toByteArray();
    QByteArray note;
    int pos = message.lastIndexOf(char(1)); // HACK
    if (pos != -1) {
        note = message.mid(pos + 1);
        message = message.left(pos);
    }
    message.replace("@CODESEG@", hexNumber(m_session.codeseg));
    message.replace("@DATASEG@", hexNumber(m_session.dataseg));
    message.replace("@PID@", hexNumber(m_session.pid));
    message.replace("@TID@", hexNumber(m_session.tid));
    sendGdbServerMessage(message, note);
}

QByteArray SymbianAdapter::trkBreakpointMessage(uint addr, uint len, bool armMode)
{
    QByteArray ba;
    appendByte(&ba, 0x82);  // unused option
    appendByte(&ba, armMode /*bp.mode == ArmMode*/ ? 0x00 : 0x01);
    appendInt(&ba, addr);
    appendInt(&ba, len);
    appendInt(&ba, 0x00000001);
    appendInt(&ba, m_session.pid);
    appendInt(&ba, 0xFFFFFFFF);
    return ba;
}

void SymbianAdapter::handleGdbServerCommand(const QByteArray &cmd)
{
    // http://sourceware.org/gdb/current/onlinedocs/gdb_34.html
    if (0) {}

    else if (cmd == "!") {
        sendGdbServerAck();
        //sendGdbServerMessage("", "extended mode not enabled");
        sendGdbServerMessage("OK", "extended mode enabled");
    }

    else if (cmd.startsWith("?")) {
        logMessage(msgGdbPacket(QLatin1String("Query halted")));
        // Indicate the reason the target halted.
        // The reply is the same as for step and continue.
        sendGdbServerAck();
        // The command below will trigger fetching a stack trace while
        // the process does not seem to be fully functional. Most notably
        // the PC points to a 0x9..., which is not in "our" range
        //sendGdbServerMessage("T05library:r;", "target halted (library load)");
        //sendGdbServerMessage("S05", "target halted (trap)");
        sendGdbServerMessage("S00", "target halted (trap)");
        //sendGdbServerMessage("O" + QByteArray("Starting...").toHex());
    }

    else if (cmd == "c") {
        logMessage(msgGdbPacket(QLatin1String("Continue")));
        sendGdbServerAck();
        QByteArray ba;
        appendByte(&ba, 0); // options
        appendInt(&ba, 0); // start address
        appendInt(&ba, 0); // end address
        appendInt(&ba, m_session.pid);
        appendInt(&ba, m_session.tid);
        sendTrkMessage(0x18, TrkCallback(), ba);
    }

    else if (cmd.startsWith("C")) {
        logMessage(msgGdbPacket(QLatin1String("Continue with signal")));
        // C sig[;addr] Continue with signal sig (hex signal number)
        //Reply: See section D.3 Stop Reply Packets, for the reply specifications.
        sendGdbServerAck();
        bool ok = false;
        uint signalNumber = cmd.mid(1).toInt(&ok, 16);
        QByteArray ba;
        appendInt(&ba, m_session.pid);
        appendInt(&ba, m_session.tid);
        sendTrkMessage(0x18, TrkCB(handleSignalContinue), ba, signalNumber);
    }

    else if (cmd.startsWith("D")) {
        sendGdbServerAck();
        sendGdbServerMessage("OK", "shutting down");
    }

    else if (cmd == "g") {
        // Read general registers.
        logMessage(msgGdbPacket(QLatin1String("Read registers")));
        sendGdbServerAck();
        sendTrkMessage(0x12, TrkCB(handleAndReportReadRegisters),
            trkReadRegisterMessage());
    }

    else if (cmd.startsWith("Hc")) {
        logMessage(msgGdbPacket(QLatin1String("Set thread & continue")));
        // Set thread for subsequent operations (`m', `M', `g', `G', et.al.).
        // for step and continue operations
        //$Hc-1#09
        sendGdbServerAck();
        sendGdbServerMessage("OK", "Set current thread for step & continue");
    }

    else if (cmd.startsWith("Hg")) {
        logMessage(msgGdbPacket(QLatin1String("Set thread")));
        // Set thread for subsequent operations (`m', `M', `g', `G', et.al.).
        // for 'other operations.  0 - any thread
        //$Hg0#df
        sendGdbServerAck();
        m_session.currentThread = cmd.mid(2).toInt(0, 16);
        sendGdbServerMessage("OK", "Set current thread "
            + QByteArray::number(m_session.currentThread));
    }

    else if (cmd == "k" || cmd.startsWith("vKill")) {
        // Kill inferior process
        logMessage(msgGdbPacket(QLatin1String("kill")));
        sendGdbServerAck();
        QByteArray ba;
        appendByte(&ba, 0); // ?
        appendByte(&ba, 0); // Sub-command: Delete Process
        appendInt(&ba, m_session.pid);
        sendTrkMessage(0x41, TrkCallback(), ba, "Delete process"); // Delete Item
        sendGdbServerMessageAfterTrkResponse("", "process killed");
    }

    else if (cmd.startsWith("m")) {
        logMessage(msgGdbPacket(QLatin1String("Read memory")));
        // m addr,length
        sendGdbServerAck();
        uint addr = 0, len = 0;
        do {
            const int pos = cmd.indexOf(',');
            if (pos == -1)
                break;
            bool ok;
            addr = cmd.mid(1, pos - 1).toUInt(&ok, 16);
            if (!ok)
                break;
            len = cmd.mid(pos + 1).toUInt(&ok, 16);
            if (!ok)
                break;
        } while (false);
        if (len) {
            readMemory(addr, len);
        } else {
            sendGdbServerMessage("E20", "Error " + cmd);
        }
    }
    else if (cmd.startsWith("p")) {
        logMessage(msgGdbPacket(QLatin1String("read register")));
        // 0xf == current instruction pointer?
        //sendGdbServerMessage("0000", "current IP");
        sendGdbServerAck();
        bool ok = false;
        const uint registerNumber = cmd.mid(1).toInt(&ok, 16);
        QByteArray logMsg = "Read Register";
        if (registerNumber == RegisterPSGdb) {
            QByteArray ba;
            appendInt(&ba, m_snapshot.registers[RegisterPSTrk], m_registerEndianness);
            logMsg += dumpRegister(registerNumber, m_snapshot.registers[RegisterPSTrk]);
            sendGdbServerMessage(ba.toHex(), logMsg);
        } else if (registerNumber < RegisterCount) {
            QByteArray ba;
            appendInt(&ba, m_snapshot.registers[registerNumber], m_registerEndianness);
            logMsg += dumpRegister(registerNumber, m_snapshot.registers[registerNumber]);
            sendGdbServerMessage(ba.toHex(), logMsg);
        } else {
            sendGdbServerMessage("0000", "read single unknown register #"
                + QByteArray::number(registerNumber));
            //sendGdbServerMessage("E01", "read single unknown register");
        }
    }

    else if (cmd == "qAttached") {
        //$qAttached#8f
        // 1: attached to an existing process
        // 0: created a new process
        sendGdbServerAck();
        sendGdbServerMessage("0", "new process created");
        //sendGdbServerMessage("1", "attached to existing process");
        //sendGdbServerMessage("E01", "new process created");
    }

    else if (cmd.startsWith("qC")) {
        logMessage(msgGdbPacket(QLatin1String("query thread id")));
        // Return the current thread ID
        //$qC#b4
        sendGdbServerAck();
        sendGdbServerMessageAfterTrkResponse("QC@TID@");
    }

    else if (cmd.startsWith("qSupported")) {
        //$qSupported#37
        //$qSupported:multiprocess+#c6
        //logMessage("Handling 'qSupported'");
        sendGdbServerAck();
        sendGdbServerMessage(
            "PacketSize=7cf;"
            "QPassSignals+;"
            "qXfer:libraries:read+;"
            //"qXfer:auxv:read+;"
            "qXfer:features:read+");
    }

    else if (cmd == "qfDllInfo") {
        // happens with  gdb 6.4.50.20060226-cvs / CodeSourcery
        // never made it into FSF gdb?
        sendGdbServerAck();
        sendGdbServerMessage("", "FIXME: nothing?");
    }

    else if (cmd == "qPacketInfo") {
        // happens with  gdb 6.4.50.20060226-cvs / CodeSourcery
        // deprecated by qSupported?
        sendGdbServerAck();
        sendGdbServerMessage("", "FIXME: nothing?");
    }

    else if (cmd == "qOffsets") {
        sendGdbServerAck();
        sendGdbServerMessageAfterTrkResponse("TextSeg=@CODESEG@;DataSeg=@DATASEG@");
    }

    else if (cmd == "qSymbol::") {
        if (m_verbose)
            logMessage(msgGdbPacket(QLatin1String("notify can handle symbol lookup")));
        // Notify the target that GDB is prepared to serve symbol lookup requests.
        sendGdbServerAck();
        if (1)
            sendGdbServerMessage("OK", "no further symbols needed");
        else
            sendGdbServerMessage("qSymbol:" + QByteArray("_Z7E32Mainv").toHex(),
                "ask for more");
    }

    else if (cmd.startsWith("qXfer:features:read:target.xml:")) {
        //  $qXfer:features:read:target.xml:0,7ca#46...Ack
        sendGdbServerAck();
        sendGdbServerMessage("l<target><architecture>symbianelf</architecture></target>");
    }

    else if (cmd == "QStartNoAckMode") {
        //$qSupported#37
        //logMessage("Handling 'QStartNoAckMode'");
        sendGdbServerAck();
        sendGdbServerMessage("OK", "ack no-ack mode");
        m_gdbAckMode = false;
    }

    else if (cmd.startsWith("QPassSignals")) {
        // list of signals to pass directly to inferior
        // $QPassSignals:e;10;14;17;1a;1b;1c;21;24;25;4c;#8f
        // happens only if "QPassSignals+;" is qSupported
        sendGdbServerAck();
        // FIXME: use the parameters
        sendGdbServerMessage("OK", "passing signals accepted");
    }

    else if (cmd == "s" || cmd.startsWith("vCont;s")) {
        logMessage(msgGdbPacket(QLatin1String("Step range")));
        logMessage("  from " + hexxNumber(m_snapshot.registers[RegisterPC]));
        sendGdbServerAck();
        m_running = true;
        QByteArray ba;
        appendByte(&ba, 0x01); // options
        appendInt(&ba, m_snapshot.registers[RegisterPC]); // start address
        appendInt(&ba, m_snapshot.registers[RegisterPC]); // end address
        appendInt(&ba, m_session.pid);
        appendInt(&ba, m_session.tid);
        sendTrkMessage(0x19, TrkCB(handleStepRange), ba, "Step range");
    }

    else if (cmd == "vCont?") {
        // actions supported by the vCont packet
        sendGdbServerAck();
        //sendGdbServerMessage("OK"); // we don't support vCont.
        sendGdbServerMessage("vCont;c;C;s;S");
    }

    else if (cmd == "vCont;c") {
        // vCont[;action[:thread-id]]...'
        sendGdbServerAck();
        m_running = true;
        sendTrkMessage(0x18, TrkCallback(), trkContinueMessage(), "CONTINUE");
    }

    else if (cmd.startsWith("Z0,") || cmd.startsWith("Z1,")) {
        // Insert breakpoint
        sendGdbServerAck();
        logMessage(msgGdbPacket(QLatin1String("Insert breakpoint")));
        // $Z0,786a4ccc,4#99
        const int pos = cmd.lastIndexOf(',');
        bool ok = false;
        const uint addr = cmd.mid(3, pos - 3).toInt(&ok, 16);
        const uint len = cmd.mid(pos + 1).toInt(&ok, 16);
        //qDebug() << "ADDR: " << hexNumber(addr) << " LEN: " << len;
        logMessage(QString::fromLatin1("Inserting breakpoint at 0x%1, %2")
            .arg(addr, 0, 16).arg(len));
        const QByteArray ba = trkBreakpointMessage(addr, len, m_session.pid);
        sendTrkMessage(0x1B, TrkCB(handleAndReportSetBreakpoint), ba, addr);
    }

    else if (cmd.startsWith("z0,") || cmd.startsWith("z1,")) {
        // Remove breakpoint
        sendGdbServerAck();
        logMessage(msgGdbPacket(QLatin1String("Remove breakpoint")));
        // $z0,786a4ccc,4#99
        const int pos = cmd.lastIndexOf(',');
        bool ok = false;
        const uint addr = cmd.mid(3, pos - 3).toInt(&ok, 16);
        const uint len = cmd.mid(pos + 1).toInt(&ok, 16);
        const uint bp = m_session.addressToBP[addr];
        if (bp == 0) {
            logMessage(QString::fromLatin1("NO RECORDED BP AT 0x%1, %2")
                .arg(addr, 0, 16).arg(len));
            sendGdbServerMessage("E00");
        } else {
            m_session.addressToBP.remove(addr);
            QByteArray ba;
            appendByte(&ba, 0x00);
            appendShort(&ba, bp);
            appendInt(&ba, addr);
            sendTrkMessage(0x1C, TrkCB(handleClearBreakpoint), ba, addr);
        }
    }

    else if (cmd.startsWith("qPart:") || cmd.startsWith("qXfer:"))  {
        QByteArray data  = cmd.mid(1 + cmd.indexOf(':'));
        // "qPart:auxv:read::0,147": Read OS auxiliary data (see info aux)
        bool handled = false;
        if (data.startsWith("auxv:read::")) {
            const int offsetPos = data.lastIndexOf(':') + 1;
            const int commaPos = data.lastIndexOf(',');
            if (commaPos != -1) {                
                bool ok1 = false, ok2 = false;
                const int offset = data.mid(offsetPos,  commaPos - offsetPos)
                    .toInt(&ok1, 16);
                const int length = data.mid(commaPos + 1).toInt(&ok2, 16);
                if (ok1 && ok2) {
                    const QString msg = QString::fromLatin1("Read of OS auxilary "
                        "vector (%1, %2) not implemented.").arg(offset).arg(length);
                    logMessage(msgGdbPacket(msg));
                    sendGdbServerMessage("E20", msg.toLatin1());
                    handled = true;
                }
            }
        } // auxv read
        if (!handled) {
            const QString msg = QLatin1String("FIXME unknown 'XFER'-request: ")
                + QString::fromAscii(cmd);
            logMessage(msgGdbPacket(msg));
            sendGdbServerMessage("E20", msg.toLatin1());
        }
    } // qPart/qXfer
    else {
        logMessage(msgGdbPacket(QLatin1String("FIXME unknown: ")
            + QString::fromAscii(cmd)));
    }
}

void SymbianAdapter::executeCommand(const QString &msg)
{
    if (msg == "EI") {
        sendGdbMessage("-exec-interrupt");
    } else if (msg == "C") {
        sendTrkMessage(0x18, TrkCallback(), trkContinueMessage(), "CONTINUE");
    } else if (msg == "R") {
        sendTrkMessage(0x18, TrkCB(handleReadRegisters),
            trkReadRegisterMessage(), "READ REGS");
    } else if (msg == "I") {
        interruptInferior();
    } else {
        logMessage("EXECUTING GDB COMMAND " + msg);
        sendGdbMessage(msg);
    }
}

void SymbianAdapter::sendTrkMessage(byte code, TrkCallback callback,
    const QByteArray &data, const QVariant &cookie)
{
    m_trkDevice.sendTrkMessage(code, callback, data, cookie);
}

void SymbianAdapter::sendTrkAck(byte token)
{
    logMessage(QString("SENDING ACKNOWLEDGEMENT FOR TOKEN %1").arg(int(token)));
    m_trkDevice.sendTrkAck(token);
}

void SymbianAdapter::handleTrkError(const QString &msg)
{
    logMessage("## TRK ERROR: " + msg);
}

void SymbianAdapter::handleTrkResult(const TrkResult &result)
{
    if (result.isDebugOutput) {
        sendTrkAck(result.token);
        logMessage(QLatin1String("APPLICATION OUTPUT: ") +
            QString::fromAscii(result.data));
        sendGdbServerMessage("O" + result.data.toHex());
        return;
    }
    logMessage("READ TRK " + result.toString());
    QByteArray prefix = "READ BUF:                                       ";
    QByteArray str = result.toString().toUtf8();
    switch (result.code) {
        case 0x80: // ACK
            break;
        case 0xff: { // NAK. This mostly means transmission error, not command failed.
            QString logMsg;
            QTextStream(&logMsg) << prefix << "NAK: for token=" << result.token
                << " ERROR: " << errorMessage(result.data.at(0)) << ' ' << str;
            logMessage(logMsg);
            break;
        }
        case 0x90: { // Notified Stopped
            // 90 01   78 6a 40 40   00 00 07 23   00 00 07 24  00 00
            const char *data = result.data.data();
            const uint addr = extractInt(data);
            const uint pid = extractInt(data + 4);
            const uint tid = extractInt(data + 8);
            logMessage(prefix + QString::fromLatin1("NOTE: PID %1/TID %2 "
                "STOPPED at 0x%3").arg(pid).arg(tid).arg(addr, 0, 16));
            sendTrkAck(result.token);
            if (addr) {
                // Todo: Do not send off GdbMessages if a synced gdb
                // query is pending, queue instead
                if (m_running) {
                    m_running = false;
                    sendGdbServerMessage("S05", "Target stopped");
                }
            } else {
                logMessage(QLatin1String("Ignoring stop at 0"));
            }
            break;
        }
        case 0x91: { // Notify Exception (obsolete)
            logMessage(prefix + "NOTE: EXCEPTION  " + str);
            sendTrkAck(result.token);
            break;
        }
        case 0x92: { //
            logMessage(prefix + "NOTE: INTERNAL ERROR: " + str);
            sendTrkAck(result.token);
            break;
        }

        // target->host OS notification
        case 0xa0: { // Notify Created
            const char *data = result.data.data();
            const byte error = result.data.at(0);
            // type: 1 byte; for dll item, this value is 2.
            const byte type = result.data.at(1);
            const uint pid = extractInt(data + 2);
            const uint tid = extractInt(data + 6);
            const uint codeseg = extractInt(data + 10);
            const uint dataseg = extractInt(data + 14);
            const uint len = extractShort(data + 18);
            const QByteArray name = result.data.mid(20, len); // library name
            m_session.modules += QString::fromAscii(name);
            QString logMsg;
            QTextStream str(&logMsg);
            str << prefix << " NOTE: LIBRARY LOAD: token=" << result.token;
            if (error)
                str << " ERROR: " << int(error);
            str << " TYPE: " << int(type) << " PID: " << pid << " TID:   " <<  tid;
            str << " CODE: " << hexxNumber(codeseg);
            str << " DATA: " << hexxNumber(dataseg);
            str << " NAME: '" << name << '\'';
            logMessage(logMsg);
            // This lets gdb trigger a register update etc
            //sendGdbServerMessage("T05library:r;");
            sendTrkMessage(0x18, TrkCallback(), trkContinueMessage(), "CONTINUE");
            break;
        }
        case 0xa1: { // NotifyDeleted
            const ushort itemType = extractByte(result.data.data() + 1);
            const ushort len = result.data.size() > 12
                ? extractShort(result.data.data() + 10) : ushort(0);
            const QString name = len
                ? QString::fromAscii(result.data.mid(12, len)) : QString();
            if (!name.isEmpty())
                m_session.modules.removeAll(name);
            logMessage(QString::fromLatin1("%1 %2 UNLOAD: %3")
                .arg(QString::fromAscii(prefix))
                .arg(itemType ? QLatin1String("LIB") : QLatin1String("PROCESS"))
                .arg(name));
            sendTrkAck(result.token);
            if (itemType == 0) {
                sendGdbServerMessage("W00", "Process exited");
                //sendTrkMessage(0x02, TrkCB(handleDisconnect));
            }
            break;
        }
        case 0xa2: { // NotifyProcessorStarted
            logMessage(prefix + "NOTE: PROCESSOR STARTED: " + str);
            sendTrkAck(result.token);
            break;
        }
        case 0xa6: { // NotifyProcessorStandby
            logMessage(prefix + "NOTE: PROCESSOR STANDBY: " + str);
            sendTrkAck(result.token);
            break;
        }
        case 0xa7: { // NotifyProcessorReset
            logMessage(prefix + "NOTE: PROCESSOR RESET: " + str);
            sendTrkAck(result.token);
            break;
        }
        default: {
            logMessage(prefix + "INVALID: " + str);
            break;
        }
    }
}

void SymbianAdapter::handleCpuType(const TrkResult &result)
{
    //---TRK------------------------------------------------------
    //  Command: 0x80 Acknowledge
    //    Error: 0x00
    // [80 03 00  04 00 00 04 00 00 00]
    m_session.cpuMajor = result.data[1];
    m_session.cpuMinor = result.data[2];
    m_session.bigEndian = result.data[3];
    m_session.defaultTypeSize = result.data[4];
    m_session.fpTypeSize = result.data[5];
    m_session.extended1TypeSize = result.data[6];
    //m_session.extended2TypeSize = result.data[6];
    QString logMsg;
    QTextStream(&logMsg) << "HANDLE CPU TYPE: CPU=" << m_session.cpuMajor << '.'
        << m_session.cpuMinor << " bigEndian=" << m_session.bigEndian
        << " defaultTypeSize=" << m_session.defaultTypeSize
        << " fpTypeSize=" << m_session.fpTypeSize
        << " extended1TypeSize=" <<  m_session.extended1TypeSize;
    logMessage(logMsg);
}

void SymbianAdapter::handleCreateProcess(const TrkResult &result)
{
    //  40 00 00]
    //logMessage("       RESULT: " + result.toString());
    // [80 08 00   00 00 01 B5   00 00 01 B6   78 67 40 00   00 40 00 00]
    const char *data = result.data.data();
    m_session.pid = extractInt(data + 1);
    m_session.tid = extractInt(data + 5);
    m_session.codeseg = extractInt(data + 9);
    m_session.dataseg = extractInt(data + 13);

    logMessage("PID: " + hexxNumber(m_session.pid));
    logMessage("TID: " + hexxNumber(m_session.tid));
    logMessage("COD: " + hexxNumber(m_session.codeseg));
    logMessage("DAT: " + hexxNumber(m_session.dataseg));

    QByteArray ba;
    appendInt(&ba, m_session.pid);
    appendInt(&ba, m_session.tid);

    startGdb();
}

void SymbianAdapter::handleReadRegisters(const TrkResult &result)
{
    logMessage("       RESULT: " + result.toString());
    // [80 0B 00   00 00 00 00   C9 24 FF BC   00 00 00 00   00
    //  60 00 00   00 00 00 00   78 67 79 70   00 00 00 00   00...]
    if (result.errorCode()) {
        logMessage("ERROR: " + result.errorString());
        return;
    }
    const char *data = result.data.data() + 1; // Skip ok byte
    for (int i = 0; i < RegisterCount; ++i)
        m_snapshot.registers[i] = extractInt(data + 4 * i);
} 

void SymbianAdapter::handleAndReportReadRegisters(const TrkResult &result)
{
    handleReadRegisters(result);
    QByteArray ba;
    for (int i = 0; i < 16; ++i) {
        const uint reg = m_registerEndianness == LittleEndian
            ? swapEndian(m_snapshot.registers[i]) : m_snapshot.registers[i];
        ba += hexNumber(reg, 8);
    }
    QByteArray logMsg = "REGISTER CONTENTS: ";
    if (m_verbose > 1) {
        for (int i = 0; i < RegisterCount; ++i) {
            logMsg += dumpRegister(i, m_snapshot.registers[i]);
            logMsg += ' ';
        }
    }
    sendGdbServerMessage(ba, logMsg);
}

static QString msgMemoryReadError(int code, uint addr, uint len = 0)
{
    const QString lenS = len ? QString::number(len) : QLatin1String("<unknown>");
    return QString::fromLatin1("Memory read error %1 at: 0x%2 %3")
        .arg(code).arg(addr, 0 ,16).arg(lenS);
}

void SymbianAdapter::handleReadMemoryBuffered(const TrkResult &result)
{
    if (extractShort(result.data.data() + 1) + 3 != result.data.size())
        logMessage("\n BAD MEMORY RESULT: " + result.data.toHex() + "\n");
    const uint blockaddr = result.cookie.toUInt();
    if (const int errorCode = result.errorCode()) {
        logMessage(msgMemoryReadError(errorCode, blockaddr));
        return;
    }
    const QByteArray ba = result.data.mid(3);
    m_snapshot.memory.insert(blockaddr, ba);
}

// Format log message for memory access with some smartness about registers
QByteArray SymbianAdapter::memoryReadLogMessage(uint addr, uint len, const QByteArray &ba) const
{
    QByteArray logMsg = "memory contents";
    if (m_verbose > 1) {
        logMsg += " addr: " + hexxNumber(addr);
        // indicate dereferencing of registers
        if (len == 4) {
            if (addr == m_snapshot.registers[RegisterPC]) {
                logMsg += "[PC]";
            } else if (addr == m_snapshot.registers[RegisterPSTrk]) {
                logMsg += "[PSTrk]";
            } else if (addr == m_snapshot.registers[RegisterSP]) {
                logMsg += "[SP]";
            } else if (addr == m_snapshot.registers[RegisterLR]) {
                logMsg += "[LR]";
            } else if (addr > m_snapshot.registers[RegisterSP] &&
                    (addr - m_snapshot.registers[RegisterSP]) < 10240) {
                logMsg += "[SP+"; // Stack area ...stack seems to be top-down
                logMsg += QByteArray::number(addr - m_snapshot.registers[RegisterSP]);
                logMsg += ']';
            }
        }
        logMsg += " length ";
        logMsg += QByteArray::number(len);
        logMsg += " :";
        logMsg += stringFromArray(ba, 16).toAscii();
    }
    return logMsg;
}

void SymbianAdapter::reportReadMemoryBuffered(const TrkResult &result)
{
    const qulonglong cookie = result.cookie.toULongLong();
    const uint addr = cookie >> 32;
    const uint len = uint(cookie);

    // Gdb accepts less memory according to documentation.
    // Send E on complete failure.
    QByteArray ba;
    uint blockaddr = (addr / MemoryChunkSize) * MemoryChunkSize;
    for (; blockaddr < addr + len; blockaddr += MemoryChunkSize) {
        const Snapshot::Memory::const_iterator it = m_snapshot.memory.constFind(blockaddr);
        if (it == m_snapshot.memory.constEnd())
            break;
        ba.append(it.value());
    }
    const int previousChunkOverlap = addr % MemoryChunkSize;
    if (previousChunkOverlap != 0 && ba.size() > previousChunkOverlap)
        ba.remove(0, previousChunkOverlap);
    if (ba.size() > int(len))
        ba.truncate(len);

    if (ba.isEmpty()) {
        ba = "E20";
        sendGdbServerMessage(ba, msgMemoryReadError(32, addr, len).toLatin1());
    } else {
        sendGdbServerMessage(ba.toHex(), memoryReadLogMessage(addr, len, ba));
    }
}

void SymbianAdapter::handleReadMemoryUnbuffered(const TrkResult &result)
{
    //logMessage("UNBUFFERED MEMORY READ: " + stringFromArray(result.data));
    const uint blockaddr = result.cookie.toUInt();
    if (extractShort(result.data.data() + 1) + 3 != result.data.size())
        logMessage("\n BAD MEMORY RESULT: " + result.data.toHex() + "\n");
    if (const int errorCode = result.errorCode()) {
        const QByteArray ba = "E20";
        sendGdbServerMessage(ba, msgMemoryReadError(32, blockaddr).toLatin1());
    } else {
        const QByteArray ba = result.data.mid(3);
        sendGdbServerMessage(ba.toHex(), memoryReadLogMessage(blockaddr, ba.size(), ba));
    }
}

void SymbianAdapter::handleStepRange(const TrkResult &result)
{
    // [80 0f 00]
    if (result.errorCode()) {
        logMessage("ERROR: " + result.errorString());
        sendGdbServerMessage("S05", "Stepping finished");
        return;
    }
    logMessage("STEPPING FINISHED ");
    // The gdb server response is triggered later by the Stop Reply packet
    //sendGdbServerMessage("S05", "Stepping finished");
}

void SymbianAdapter::handleAndReportSetBreakpoint(const TrkResult &result)
{
    //---TRK------------------------------------------------------
    //  Command: 0x80 Acknowledge
    //    Error: 0x00
    // [80 09 00 00 00 00 0A]
    uint bpnr = extractByte(result.data.data());
    uint addr = result.cookie.toUInt();
    m_session.addressToBP[addr] = bpnr;
    logMessage("SET BREAKPOINT " + hexxNumber(bpnr) + " "
         + stringFromArray(result.data.data()));
    sendGdbServerMessage("OK");
    //sendGdbServerMessage("OK");
}

void SymbianAdapter::handleClearBreakpoint(const TrkResult &result)
{
    logMessage("CLEAR BREAKPOINT ");
    if (result.errorCode()) {
        logMessage("ERROR: " + result.errorString());
        //return;
    } 
    sendGdbServerMessage("OK");
}

void SymbianAdapter::handleSignalContinue(const TrkResult &result)
{
    int signalNumber = result.cookie.toInt();
    logMessage("   HANDLE SIGNAL CONTINUE: " + stringFromArray(result.data));
    logMessage("NUMBER" + QString::number(signalNumber));
    sendGdbServerMessage("O" + QByteArray("Console output").toHex());
    sendGdbServerMessage("W81"); // "Process exited with result 1
}

void SymbianAdapter::handleSupportMask(const TrkResult &result)
{
    const char *data = result.data.data();
    QByteArray str;
    for (int i = 0; i < 32; ++i) {
        //str.append("  [" + formatByte(data[i]) + "]: ");
        for (int j = 0; j < 8; ++j)
        if (data[i] & (1 << j))
            str.append(QByteArray::number(i * 8 + j, 16));
    }
    logMessage("SUPPORTED: " + str);
 }

void SymbianAdapter::handleTrkVersions(const TrkResult &result)
{
    QString logMsg;
    QTextStream str(&logMsg);
    str << "Versions: ";
    if (result.data.size() >= 5) {
        str << "Trk version " << int(result.data.at(1)) << '.'
            << int(result.data.at(2))
            << ", Protocol version " << int(result.data.at(3))
             << '.' << int(result.data.at(4));
    }
    logMessage(logMsg);
}

void SymbianAdapter::handleDisconnect(const TrkResult & /*result*/)
{
    logMessage(QLatin1String("Trk disconnected"));
}

void SymbianAdapter::readMemory(uint addr, uint len)
{
    Q_ASSERT(len < (2 << 16));

    // We try to get medium-sized chunks of data from the device
    if (m_verbose > 2)
        logMessage(QString::fromLatin1("readMemory %1 bytes from 0x%2 blocksize=%3")
            .arg(len).arg(addr, 0, 16).arg(MemoryChunkSize));

    if (m_bufferedMemoryRead) {
        uint blockaddr = (addr / MemoryChunkSize) * MemoryChunkSize;
        for (; blockaddr < addr + len; blockaddr += MemoryChunkSize) {
            if (!m_snapshot.memory.contains(blockaddr)) {
                if (m_verbose)
                    logMessage(QString::fromLatin1("Requesting buffered "
                        "memory %1 bytes from 0x%2")
                    .arg(MemoryChunkSize).arg(blockaddr, 0, 16));
                sendTrkMessage(0x10, TrkCB(handleReadMemoryBuffered),
                    trkReadMemoryMessage(blockaddr, MemoryChunkSize),
                    QVariant(blockaddr));
            }
        }
        const qulonglong cookie = (qulonglong(addr) << 32) + len;
        sendTrkMessage(TRK_WRITE_QUEUE_NOOP_CODE, TrkCB(reportReadMemoryBuffered),
            QByteArray(), cookie);
    } else {
        if (m_verbose)
            logMessage(QString::fromLatin1("Requesting unbuffered memory %1 "
                "bytes from 0x%2").arg(len).arg(addr, 0, 16));
        sendTrkMessage(0x10, TrkCB(handleReadMemoryUnbuffered),
           trkReadMemoryMessage(addr, len), QVariant(addr));
    }
}

void SymbianAdapter::interruptInferior()
{
    QByteArray ba;
    // stop the thread (2) or the process (1) or the whole system (0)
    // We choose 2, as 1 does not seem to work.
    appendByte(&ba, 2);
    appendInt(&ba, m_session.pid);
    appendInt(&ba, m_session.tid); // threadID: 4 bytes Variable number of bytes.
    sendTrkMessage(0x1a, TrkCallback(), ba, "Interrupting...");
}

void SymbianAdapter::handleGdbError(QProcess::ProcessError error)
{
    emit output(QString("GDB: Process Error %1: %2").arg(error).arg(errorString()));
}

void SymbianAdapter::handleGdbFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    emit output(QString("GDB: ProcessFinished %1 %2").arg(exitCode).arg(exitStatus));
}

void SymbianAdapter::handleGdbStarted()
{
    emit output(QString("GDB: Process Started"));
    emit started();
}

void SymbianAdapter::handleGdbStateChanged(QProcess::ProcessState newState)
{
    emit output(QString("GDB: Process State %1").arg(newState));
}

void SymbianAdapter::run()
{
    emit output("### Starting SymbianAdapter");
    m_rfcommProc.start("rfcomm listen " + m_rfcommDevice + " 1");
    m_rfcommProc.waitForStarted();
    
    if (m_rfcommProc.state() != QProcess::Running) {
        emit finished(-44, QProcess::CrashExit);
        return;
    }

    connect(&m_trkDevice, SIGNAL(messageReceived(trk::TrkResult)),
        this, SLOT(handleTrkResult(trk::TrkResult)));
    connect(&m_trkDevice, SIGNAL(error(QString)),
        this, SLOT(handleTrkError(QString)));

    startInferior();
}

void SymbianAdapter::startGdb()
{
    if (!m_gdbServer.listen(QHostAddress(gdbServerIP()), gdbServerPort())) {
        logMessage(QString("Unable to start the gdb server at %1: %2.")
            .arg(m_gdbServerName).arg(m_gdbServer.errorString()));
        emit finished(-45, QProcess::CrashExit);
        return;
    }

    logMessage(QString("Gdb server running on %1.\nRegister endianness: %3.")
        .arg(m_gdbServerName).arg(m_registerEndianness));

    connect(&m_gdbServer, SIGNAL(newConnection()),
        this, SLOT(handleGdbConnection()));

    logMessage("STARTING GDB");
    QStringList gdbArgs;
    gdbArgs.append("--nx"); // Do not read .gdbinit file
    gdbArgs.append("-i");
    gdbArgs.append("mi");
    m_gdbProc.start(QDir::currentPath() + "/cs-gdb", gdbArgs);
}

void SymbianAdapter::sendGdbMessage(const QString &msg, GdbCallback callback,
    const QVariant &cookie)
{
    GdbCommand data;
    data.command = msg;
    data.callback = callback;
    data.cookie = cookie;
    logMessage(QString("<- ADAPTER TO GDB: %2").arg(msg));
    m_gdbProc.write(msg.toLatin1() + "\n");
}

//
// GdbProcessBase
//

void SymbianAdapter::handleRfcommReadyReadStandardError()
{
    QByteArray ba = m_rfcommProc.readAllStandardError();
    emit output(QString("RFCONN stderr: %1").arg(QString::fromLatin1(ba)));
}

void SymbianAdapter::handleRfcommReadyReadStandardOutput()
{
    QByteArray ba = m_rfcommProc.readAllStandardOutput();
    emit output(QString("RFCONN stdout: %1").arg(QString::fromLatin1(ba)));
}


void SymbianAdapter::handleRfcommError(QProcess::ProcessError error)
{
    emit output(QString("RFCOMM: Process Error %1: %2").arg(error).arg(errorString()));
}

void SymbianAdapter::handleRfcommFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    emit output(QString("RFCOMM: ProcessFinished %1 %2").arg(exitCode).arg(exitStatus));
}

void SymbianAdapter::handleRfcommStarted()
{
    emit output(QString("RFCOMM: Process Started"));
}

void SymbianAdapter::handleRfcommStateChanged(QProcess::ProcessState newState)
{
    emit output(QString("RFCOMM: Process State %1").arg(newState));
}

//
// GdbProcessBase
//

void SymbianAdapter::start(const QString &program, const QStringList &args,
    QIODevice::OpenMode mode)
{
    Q_UNUSED(mode);
    run();
}

void SymbianAdapter::kill()
{
    m_gdbProc.kill();
}

void SymbianAdapter::terminate()
{
    m_gdbProc.terminate();
}

bool SymbianAdapter::waitForFinished(int msecs)
{
    return m_gdbProc.waitForFinished(msecs);
}

QProcess::ProcessState SymbianAdapter::state() const
{
    return m_gdbProc.state();
}

QString SymbianAdapter::errorString() const
{
    return m_gdbProc.errorString();
}

QByteArray SymbianAdapter::readAllStandardError()
{
    return m_gdbProc.readAllStandardError();
}

QByteArray SymbianAdapter::readAllStandardOutput()
{
    return m_gdbProc.readAllStandardOutput();
}

qint64 SymbianAdapter::write(const char *data)
{
    return m_gdbProc.write(data);
}

void SymbianAdapter::setWorkingDirectory(const QString &dir)
{
    m_gdbProc.setWorkingDirectory(dir);
}

void SymbianAdapter::setEnvironment(const QStringList &env)
{
    m_gdbProc.setEnvironment(env);
}

void SymbianAdapter::attach(GdbEngine *engine) const
{
#ifdef STANDALONE_RUNNER
#else
    QString fileName = engine->startParameters().executable; 
    engine->postCommand(_("add-symbol-file \"%1\" %2").arg(fileName)
        .arg(m_session.codeseg));
    engine->postCommand(_("symbol-file \"%1\"").arg(fileName));
    engine->postCommand(_("target remote ") + gdbServerName());
    engine->attemptBreakpointSynchronization();
#endif
}

} // namespace Internal
} // namespace Debugger
