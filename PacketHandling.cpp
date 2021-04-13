//
// Created by Timmy on 4/13/2021.
//
/* Thread1 puts faulty packet into queue
 * thread2 takes faulty packets, feeds it to GPU, without waiting for it to finish. Run this in a loop?
 * Once a packet is finished flipping, calculate crcs and compare.
 * Feed finished packet into queue that is retrieved by thread1 and used.



MAIN_DISPLAY_THREAD:
    lock(finished_packets);
    while (finished_packets != EMPTY){
        display(finished_packets.pop());
    }
    unlock(finished_packets);


MAIN_PACKET_THREAD:
    packet = retrieve_packet();
    if (packet_needs_fixing){
        lock(unfinished_pack_queue);
        unfinished_pack_queue.add(packet);
        unlock(unfinished_pack_queue);
    }
    else{
        lock(finished_packets);
        finished_packets.add(packet);
        unlock(finished_packets);

    }


GPU_LOADER_THREAD:
    lock(unfinished_pack_queue);
    while (unfinished_pack_queue != EMPTY){
        feed_to_gpu(unfinished_pack_queue.pop());
    }
    unlock(unfinished_pack_queue);
    spin_wait(gpu);

    for (returned_packets: gpu){
        calculate_crcs();
        if (fixed){
            lock(finished_packets) // else spinlock
            add_to_finished_packets();
            unlock(finished_packets)
        }
    }



 */

