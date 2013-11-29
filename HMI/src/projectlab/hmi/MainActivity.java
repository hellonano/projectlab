package projectlab.hmi;

import java.io.IOException;
import java.io.InputStream;
import java.io.PrintWriter;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketAddress;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.List;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.View.OnTouchListener;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import com.example.test.R;

public class MainActivity extends Activity implements OnClickListener, OnTouchListener, OnItemClickListener{
	public static final int STATUS_READY = 0;
	public static final int STATUS_CONNECTING = 1;
	public static final int STATUS_CONNECTED = 2;
	public static final int STATUS_PAUSED = 3;
	public static final int STATUS_ERROR = 4;
	public static final int LEFT = 0;
	public static final int RIGHT = 1;
	public static final int STOP = 2;
	public static final int GO = 3;
	public static final int BACK = 4;
	int status = STATUS_READY;
	public static final int TIMEOUT_CONNECT = 5000;
	
	public static final int PORT = 7777;;	//	항체 소켓 연결 포트
	
	ArrayList<ServerItem> serverlist = new ArrayList<MainActivity.ServerItem>();
	Socket socket;
	String ip;
	
	ImageView iv_control_button;
	Button btn_c, btn_f, btn_l, btn_r, btn_b;
	LinearLayout cover;
	EditText et_ip;
	ListView listview;
	
	Handler handler = new Handler();	//	스레드 UI 처리 핸들러

	InputStream is;			//	이미지 수신 스트림
	PrintWriter pw;			//	문자 송신 스트림
	Bitmap image;			//	수신 이미지 저장 
	Thread threadRecv;		//	이미 수신 및 출력 스레드
	ServerAdapter adapter;	//	서버 리스트 어댑
	
	int cnt;
	long time_start;
	long time_end;
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		setContentView(R.layout.activity_test);
		
		ConnectivityManager cManager; 
		NetworkInfo mobile; 
		NetworkInfo wifi; 
		 
		cManager=(ConnectivityManager)getSystemService(Context.CONNECTIVITY_SERVICE); 
		mobile = cManager.getNetworkInfo(ConnectivityManager.TYPE_MOBILE); 
		wifi = cManager.getNetworkInfo(ConnectivityManager.TYPE_WIFI); 
		 
		if(!mobile.isConnected() && !wifi.isConnected())
		{
		    Toast.makeText(getApplicationContext(), "네트워크 연결이 필요합니다.", Toast.LENGTH_SHORT).show();
		    Intent intent = new Intent("android.settings.SETTINGS");
			intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
			startActivity(intent);
		}

		iv_control_button = (ImageView) findViewById(R.id.iv_control_button);
		btn_c = (Button) findViewById(R.id.btn_c);
		btn_f = (Button) findViewById(R.id.btn_f);
		btn_l = (Button) findViewById(R.id.btn_l);
		btn_r = (Button) findViewById(R.id.btn_r);
		btn_b = (Button) findViewById(R.id.btn_b);
		cover = (LinearLayout) findViewById(R.id.cover);
		et_ip = (EditText)findViewById(R.id.et_ip);
		listview = (ListView)findViewById(R.id.listview);
		
		/*
		 * 이벤트 리스너 등록
		 */
		iv_control_button.setOnClickListener(this);
		btn_c.setOnClickListener(this);
		btn_f.setOnTouchListener(this);
		btn_l.setOnTouchListener(this);
		btn_r.setOnTouchListener(this);
		btn_b.setOnTouchListener(this);
		adapter = new ServerAdapter(this, R.layout.serverlist, serverlist);
		listview.setOnItemClickListener(this);
		listview.setAdapter(adapter);
		listview.setItemChecked(0, true);
		
		
		/*
		 * 서버 IP 리스트 추가
		 */
		serverlist.add(new ServerItem("랩실 IP", "168.188.43.121"));
		serverlist.add(new ServerItem("랩실 내부 IP", "192.168.0.2"));
		serverlist.add(new ServerItem("", "사용자 지정 IP"));
		serverlist.get(0).isChecked = true;
		adapter.notifyDataSetChanged();
		et_ip.setText(serverlist.get(0).ip);
		
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		getMenuInflater().inflate(R.menu.test, menu);
		return true;
	}

	
	/*
	 * 버튼 클릭 리스너
	 */
	@Override
	public void onClick(View v) {
		if (v.getId() == R.id.btn_c) {
			if(et_ip.getText().toString().equals(""))
				Toast.makeText(getApplicationContext(), "IP를 정확히 입력해주세요.", Toast.LENGTH_SHORT).show();
			else {
				ip = et_ip.getText().toString();
				new Connect().execute(0);
			}
		}
	}

	/*
	 * 소켓 연결
	 */
	public class Connect extends AsyncTask<Integer, Integer, Integer> {
		private static final int CONNECT_SUSSCESS = 0;
		private static final int CONNECT_FAIL_TIMEOUT = 1;
		private static final int CONNECT_FAIL_ARGUMENT = 2;
		private static final int CONNECT_FAIL = 3;
		AlertDialog dialog_connect;
		AlertDialog dialog_fail;
		@Override
		protected void onPostExecute(Integer result) {
			super.onPostExecute(result);
			dialog_connect.dismiss();
			switch(result) {
			case CONNECT_SUSSCESS :
				//	연결 성공시 스레드 생성 및 실행
				cnt = 0;
				time_start = Calendar.getInstance().getTimeInMillis();
				cover.setVisibility(View.GONE);
				findViewById(R.id.layout_control_button).setVisibility(View.VISIBLE);
				status = STATUS_CONNECTED;
				threadRecv = new Thread(runRecv); 
				threadRecv.start();
				break;
			case CONNECT_FAIL_TIMEOUT :
				//	연결 실패시 경고 메세지 출력
				status = STATUS_READY;
				dialog_fail = new AlertDialog.Builder(MainActivity.this)
					.setTitle("경고")
					.setMessage("연결에 실패했습니다.")
					.setNegativeButton("확인", null)
					.create();
				dialog_fail.show();
				cover.setVisibility(View.VISIBLE);
				break;
			default :
				Log.w("myLog","Connect Fail");
			}
			
		}

		@Override
		protected void onPreExecute() {
			super.onPreExecute();
			//	연결 다이얼로그 출력
			dialog_connect = new AlertDialog.Builder(MainActivity.this)
					.setTitle("소켓 연결")
					.setMessage("연결을 시도하고 있습니다.")
					.create();
			dialog_connect.setCancelable(false);
			dialog_connect.show();
		}

		@Override
		protected Integer doInBackground(Integer... params) {
			//	소켓 생성 및 연결 시도
			try {
				status = STATUS_CONNECTING;
				SocketAddress socketAddress = new InetSocketAddress(ip, PORT);
				socket = new Socket();
				socket.connect(socketAddress, TIMEOUT_CONNECT);
				socket.setSoTimeout(TIMEOUT_CONNECT);
				is = socket.getInputStream();
				pw = new PrintWriter(socket.getOutputStream());
				return CONNECT_SUSSCESS;
			} catch (IllegalArgumentException e) {
				e.printStackTrace();
				return CONNECT_FAIL_ARGUMENT;
			} catch (IOException e ) {
				e.printStackTrace();
				return CONNECT_FAIL_TIMEOUT;
			} catch (Exception e) {
				e.printStackTrace();
				return CONNECT_FAIL;
			}
		}
		
	};
	
	/*
	 * 영상 스레드 메소드
	 */
	Runnable runRecv = new Runnable() {
		@Override
		public void run() {
			while (status == STATUS_CONNECTED) {
				try {
					int image_size = 0;		//	수신할 이미지 크기
					int total_size = 0;		//	수신한 이미지 크기
					int read_size = 0;		//	소켓 버퍼에서 읽은 데이터 크기
					byte[] buffer_size = new byte[4];
					byte[] buffer_read = new byte[1024];
					byte[] buffer_image = new byte[1];
					
					//	이미지 크기 다운로드
					if (status == STATUS_CONNECTED && is.read(buffer_size, 0, 4) == -1) {
						break;
					}
				
					for(int i=0; i< 4; i++) {
						image_size += ((buffer_size[i] & 0xFF) << ((3-i)*8));
					}
					
					//	이미지 수신
					//	이미지 한 개를 모두 수신할 때까지 반복
					while (status == STATUS_CONNECTED && (read_size = is.read(buffer_read, 0, image_size - total_size >= 1024 ? 1024 : image_size - total_size)) >= 1) {
						total_size += read_size;
						byte[] byte_image = new byte[total_size];
						System.arraycopy(buffer_image, 0, byte_image, 0, buffer_image.length);
						System.arraycopy(buffer_read, 0, byte_image, total_size - read_size, read_size);
						buffer_image = byte_image;
						
						if (image_size == total_size) {
							//	이미지 한 개를 수신하면 디코드 하여 이미지 객체로 변환
							image = BitmapFactory.decodeByteArray(buffer_image, 0, buffer_image.length);
							if (image != null) {
								cnt++;
								handler.post(runDisplay);
							}
							break;
						}		
					}
						
					if (read_size < 1) {
						break;
					}
					
				} catch (Exception e) {
					//	수신 중 에러가 발생하면 루프 탈출
					e.printStackTrace();
					if(status != STATUS_PAUSED)
						status = STATUS_ERROR;
					break;
				}
			}
			
			handler.post(new Runnable() {
				@Override
				public void run() {
					//	에러러 발생하여 루프를 탈출하면 동작 정지 및 초기화면 복귀
					stopThread();
					if(status != STATUS_PAUSED)
						status = STATUS_READY;
				}
			});
			
		}
	};
	
	/*
	 * 	이미지 디스플레이
	 */
	Runnable runDisplay = new Runnable() {
		
		@Override
		public void run() {
			iv_control_button.setImageBitmap(image);
		}
	};
	
	/*
	 * 	버튼 터치 리스너(non-Javadoc)
	 *  버튼이 눌렸을 때 -> 동작 메세지 전송
	 *  버튼에서 손이 떼어졌을 때 -> 정지 메세지 전송
	 */
	@Override
	public boolean onTouch(View v, MotionEvent event) {
		if(event.getAction() == MotionEvent.ACTION_DOWN) {
			switch(v.getId()) {
			case R.id.btn_f :
				pw.print("f");
				break;
			case R.id.btn_b :
				pw.print("b");
				break;
			case R.id.btn_r :
				pw.print("r");
				break;
			case R.id.btn_l :
				pw.print("l");
				break;
			}
			v.setPressed(true);
		}else if(event.getAction() == MotionEvent.ACTION_UP) {
			pw.print('s');
			v.setPressed(false);
		}
		pw.flush();
		return true;
	}
	
	/*
	 * 영상 스레드 종료
	 * 종료 후 초기화면 복귀
	 */
	public void stopThread() {
		time_end = Calendar.getInstance().getTimeInMillis();
		long fps = time_end - time_start;
		Log.w("myLog","" + time_start + " / " + time_end + " / " + cnt);
		Toast.makeText(getApplicationContext(), "FPS " + time_start + " / " + time_end + " / " + cnt, Toast.LENGTH_LONG).show();
		if(status == STATUS_ERROR) {
			//	에러가 발생하여 동작을 중지
			//	사용자에게 메세지 출력
			new AlertDialog.Builder(this)
				.setTitle("경고")
				.setMessage("연결이 종료됐습니다.")
				.setNegativeButton("확인", null)
				.create().show();
		}else {
			//	종료버튼을 누르거나 화면 제어권 상실
			//	항체로 종료 메세지 전송
			pw.print('q');
			pw.flush();
			if (pw.checkError()) {
			}
		}

		//	스레드 종료 및 자원회수
		try {
			threadRecv.join();
			pw.close();
			is.close();
			socket.close();
			socket = null;
		} catch (Exception e) {
			e.printStackTrace();
		}
		
		//	초기화면 복귀
		cover.setVisibility(View.VISIBLE);
		findViewById(R.id.layout_control_button).setVisibility(View.GONE);
	}

	/*
	 * 서버리스트 목록 클릭 리스너
	 */
	@Override
	public void onItemClick(AdapterView<?> arg0, View arg1, int position, long arg3) {
		for(int i=0; i<serverlist.size(); i++) 
			serverlist.get(i).isChecked = false;
		serverlist.get(position).isChecked = true;
		if(serverlist.get(position).name.equals("")) {
			et_ip.setVisibility(View.VISIBLE);
			et_ip.setText("");
		}else {
			et_ip.setVisibility(View.INVISIBLE);
			et_ip.setText(serverlist.get(position).ip);
		}
		adapter.notifyDataSetChanged();
	}
	
	/*
	 * 	키 이벤트 리스너(non-Javadoc)
	 * 	뒤로가기 버튼을 눌렀을 때 -> 연결 종료 여부 확인
	 */
	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		if (status == STATUS_CONNECTED && keyCode == KeyEvent.KEYCODE_BACK) {
			confirmStop();
			return true;
		}
		return super.onKeyDown(keyCode, event);
	}

	/*
	 * 메뉴 리스너
	 * 종료 버튼을 눌렀을 때 -> 연결 종료 여부 확인
	 */
	@Override
	public boolean onOptionsItemSelected(MenuItem item) {
	    switch (item.getItemId()) {
	        case R.id.action_stop :
	        	if(status == STATUS_READY)
	        		finish();
	        	else if(status == STATUS_CONNECTED)
	        		confirmStop();
	            return true;
	        default:
	            return super.onOptionsItemSelected(item);
	    }
	}
	
	/*
	 * 연결 종료 여부 확인
	 */
	private void confirmStop() {
		AlertDialog dialog = new AlertDialog.Builder(this)
		.setTitle("경고")
		.setMessage("연결을 종료할까요?")
		.setPositiveButton("종료", new DialogInterface.OnClickListener() {
			
			@Override
			public void onClick(DialogInterface arg0, int arg1) {
				stopThread();
				status = STATUS_READY;
			}
		}).setNegativeButton("취소", new DialogInterface.OnClickListener() {
			
			@Override
			public void onClick(DialogInterface dialog, int which) {
				dialog.dismiss();
			}
		}).create();
		dialog.show();
	}


	class ServerItem {
		String name;
		String ip;
		boolean isChecked = false;;
		public ServerItem(String name, String ip) {
			super();
			this.name = name;
			this.ip = ip;
		}
	}
	
	public class ServerAdapter extends ArrayAdapter<ServerItem> {
		int layout;
		Context context;
		public ServerAdapter(Context context, int textViewResourceId,
				List<ServerItem> objects) {
			super(context, textViewResourceId, objects);
			layout = textViewResourceId;
			this.context = context;
		}
		
		@Override
		public View getView(int position, View convertView, ViewGroup parent) {
			LinearLayout li;
			if(convertView==null) {
				li = new LinearLayout(getContext());
				LayoutInflater infla = (LayoutInflater) getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);
				infla.inflate(layout, li);
			} else
				li = (LinearLayout) convertView;
			
			TextView tv_server_ip = (TextView) li.findViewById(R.id.tv_server_ip);
			TextView tv_server_name = (TextView) li.findViewById(R.id.tv_server_name);
			tv_server_ip.setText(getItem(position).ip);
			tv_server_name.setText(getItem(position).name);
			if(getItem(position).isChecked)
				li.setBackgroundColor(getResources().getColor(android.R.color.holo_blue_bright));
			else
				li.setBackground(null);
			return li;
		}
	}

	@Override
	protected void onPause() {
		super.onPause();
		if(status == STATUS_CONNECTED) {
			status = STATUS_PAUSED;
			stopThread();
		}
	}

	@Override
	protected void onResume() {
		super.onResume();
		if(status == STATUS_PAUSED) {
			new Connect().execute(0);
		}
	}	
}

