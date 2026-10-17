package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class RelayWorkSignalTest {
    @Test fun receiveWakeIsNotLostBetweenEmptyOutputAndWait(){
        val signal=RelayWorkSignal()
        signal.setActiveWork(false)
        val before=signal.revision()
        signal.signal()
        val returned=CountDownLatch(1)
        val worker=Thread { signal.awaitPassive(before);returned.countDown() }
        worker.start()
        try { assertTrue(returned.await(1,TimeUnit.SECONDS)) }
        finally { signal.shutdown();worker.join(1000) }
    }

    @Test fun passiveWriterWakesForReceiveResumeAndClose(){
        val signal=RelayWorkSignal()
        signal.setActiveWork(false)
        fun parkedWriter():Pair<Thread,CountDownLatch>{
            val observed=signal.revision()
            val done=CountDownLatch(1)
            val worker=Thread { signal.awaitPassive(observed);done.countDown() }
            worker.start()
            return worker to done
        }
        val (receiveWorker,receiveDone)=parkedWriter()
        try {
            assertFalse(receiveDone.await(30,TimeUnit.MILLISECONDS))
            signal.signal()
            assertTrue(receiveDone.await(1,TimeUnit.SECONDS))
            val (resumeWorker,resumeDone)=parkedWriter()
            try {
                signal.setActiveWork(true)
                assertTrue(resumeDone.await(1,TimeUnit.SECONDS))
            }finally{resumeWorker.join(1000)}
            signal.setActiveWork(false)
            val (closeWorker,closeDone)=parkedWriter()
            try {
                signal.shutdown()
                assertTrue(closeDone.await(1,TimeUnit.SECONDS))
            }finally{closeWorker.join(1000)}
        }finally{signal.shutdown();receiveWorker.join(1000)}
    }
}
