extern crate futures;
extern crate tokio;
extern crate env_logger;

use std::env;
use std::net::SocketAddr;
use futures::prelude::*;
use futures::future::{lazy, ok};
use tokio::prelude::*;
use tokio::runtime::current_thread::Runtime;
use tokio::io::copy;
use tokio::net::{TcpStream, TcpListener};

fn main() {
    env_logger::init();

    let listen_addr = env::args().nth(1).unwrap_or("127.0.0.1:6380".to_string());
    let listen_addr = listen_addr.parse::<SocketAddr>().unwrap();

    let server_addr = env::args().nth(2).unwrap_or("127.0.0.1:6379".to_string());
    let server_addr = server_addr.parse::<SocketAddr>().unwrap();

    let mut runtime = Runtime::new().unwrap();
    runtime.spawn(lazy(move || {
        let socket = TcpListener::bind(&listen_addr).unwrap();
        println!("listening on: {}", listen_addr);
        println!("proxying to: {}", server_addr);

        let worker = socket.incoming()
            .map_err(|_| ())
            .for_each(move |client| {
                let server_conn = TcpStream::connect(&server_addr);
                let inner = server_conn
                    .and_then(move |server| {
                        let (server_rx, server_tx) = server.split();
                        let (client_rx, client_tx) = client.split();

                        let client_to_server = copy(client_rx, server_tx);
                        let server_to_client = copy(server_rx, client_tx);

                        client_to_server.join(server_to_client)
                    })
                    .map_err(|_| ())
                    .map(|_| ());

                tokio::spawn(inner);
                ok(())
            });

        tokio::spawn(worker)
    }));
    runtime.run().unwrap()
}
